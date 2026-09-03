param(
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [string]$BaselineDir = "",
    [string]$Config = "",
    [string]$BuildDir = "",
    [uint32[]]$Seeds = @(
        2026090301, 2026090302, 2026090303, 2026090304,
        2026090305, 2026090306, 2026090307, 2026090308,
        2026090309, 2026090310, 2026090311, 2026090312,
        2026090313, 2026090314, 2026090315, 2026090316,
        2026090317, 2026090318, 2026090319, 2026090320,
        2026090321, 2026090322, 2026090323, 2026090324,
        2026090325, 2026090326, 2026090327, 2026090328,
        2026090329, 2026090330, 2026090331, 2026090332
    ),
    [int]$DurationMs = 1575,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Config) { $Config = Join-Path $root "config.toml" }
$Config = (Resolve-Path -LiteralPath $Config).Path
if (-not $BuildDir) {
    $BuildDir = Join-Path $root "native\vision_native\build"
}
$exe = Join-Path $BuildDir "Release\cod_native_sustained_aimlab_benchmark.exe"
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if ($Seeds.Count -eq 0) { throw "at least one seed is required" }
if ($DurationMs -lt 1575) {
    throw "duration must retain one complete fixed 1575 ms target slot"
}
if (Test-Path -LiteralPath $OutputDir) {
    if ((Get-ChildItem -LiteralPath $OutputDir -Force | Measure-Object).Count -gt 0) {
        throw "refusing to overwrite non-empty output directory: $OutputDir"
    }
} else {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

if (-not $SkipBuild) {
    & $cmake --build $BuildDir --config Release `
        --target cod_native_sustained_aimlab_benchmark -- /m
    if ($LASTEXITCODE -ne 0) { throw "benchmark build failed" }
}
if (-not (Test-Path -LiteralPath $exe)) {
    throw "benchmark executable not found: $exe"
}

$revision = (& git -C $root rev-parse HEAD).Trim()
$dirty = @(& git -C $root status --porcelain).Count -gt 0
$arms = @(
    [pscustomobject]@{
        Name = "00-static-clear"; Pov = "off"; Fire = $false
    },
    [pscustomobject]@{
        Name = "10-pov-clear"; Pov = "seeded-combined"; Fire = $false
    },
    [pscustomobject]@{
        Name = "01-static-fire"; Pov = "off"; Fire = $true
    },
    [pscustomobject]@{
        Name = "11-pov-fire"; Pov = "seeded-combined"; Fire = $true
    }
)

$summaries = @()
foreach ($arm in $arms) {
    $output = Join-Path $OutputDir "$($arm.Name).json"
    $arguments = @(
        "--config", $Config,
        "--output", $output,
        "--revision", $revision,
        "--duration-ms", [string]$DurationMs,
        "--controller-tick-hz", "1000",
        "--profile", "pure",
        "--cohort", "bodylock",
        "--target-motion-preset", "seeded-legacy",
        "--pov-motion-preset", $arm.Pov,
        "--target-slot-ms", "1575",
        "--slowdown-edge", "0.50",
        "--slowdown-center", "0.40",
        "--smoke"
    )
    if ($arm.Fire) {
        $arguments += @(
            "--vision-disturbance", "gun-kick-plus-recoil",
            "--benchmark-recoil", "on",
            "--short-occlusion-ms", "48",
            "--short-occlusion-evidence", "same-generation-cue"
        )
    } else {
        # Keep the evidence mode frozen across all four arms. With zero
        # occlusion this executes no cue but preserves paired covariates.
        $arguments += @(
            "--vision-disturbance", "off",
            "--benchmark-recoil", "off",
            "--short-occlusion-ms", "0",
            "--short-occlusion-evidence", "same-generation-cue"
        )
    }
    foreach ($seed in $Seeds) {
        $arguments += @("--seed", [string]$seed)
    }
    if ($dirty) { $arguments += "--dirty" }

    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "benchmark failed: $($arm.Name)" }
    $report = Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
    $runs = @($report.runs)
    if ($runs.Count -ne $Seeds.Count) {
        throw "$($arm.Name) expected $($Seeds.Count) runs, got $($runs.Count)"
    }
    foreach ($run in $runs) {
        if ([int]$run.targets_spawned -ne 1 -or
            [int]$run.bodylock_entry_failures -ne 0) {
            throw "$($arm.Name) seed $($run.seed) failed the BodyLock entry gate"
        }
        if ($arm.Pov -eq "off") {
            if ([double]$run.max_abs_left_x -ne 0.0 -or
                [int]$run.left_strafe_active_ms -ne 0 -or
                [int]$run.player_vertical_active_ms -ne 0) {
                throw "$($arm.Name) seed $($run.seed) contains unexpected POV motion"
            }
        } elseif ([double]$run.max_abs_left_x -le 0.0 -or
                  [int]$run.left_strafe_active_ms -le 0 -or
                  [int]$run.player_vertical_active_ms -le 0) {
            throw "$($arm.Name) seed $($run.seed) did not execute combined POV motion"
        }
        if ($arm.Fire) {
            if ([int]$run.firing_frames -le 0 -or
                [int]$run.recoil_active_frames -le 0 -or
                [int]$run.cue_continuation_frames -le 0 -or
                [int]$run.occlusion_episodes -le 0 -or
                [int]$run.post_occlusion_samples -le 0) {
                throw "$($arm.Name) seed $($run.seed) did not execute firing/recoil/cue coverage"
            }
        } elseif ([int]$run.firing_frames -ne 0 -or
                  [int]$run.recoil_active_frames -ne 0 -or
                  [int]$run.cue_continuation_frames -ne 0 -or
                  [int]$run.occlusion_episodes -ne 0) {
            throw "$($arm.Name) seed $($run.seed) contains unexpected firing/occlusion"
        }
    }

    if ($BaselineDir) {
        $baseline = Join-Path $BaselineDir "$($arm.Name).json"
        if (-not (Test-Path -LiteralPath $baseline)) {
            throw "baseline artifact not found: $baseline"
        }
        & (Join-Path $PSScriptRoot "compare_sustained_aimlab.ps1") `
            -Baseline $baseline -Candidate $output
        if ($LASTEXITCODE -ne 0) {
            throw "protected comparison failed: $($arm.Name)"
        }
    }

    $summaries += [pscustomobject]@{
        arm = $arm.Name
        runs = $runs.Count
        acquire_points = [double](
            ($runs | Measure-Object acquire_points -Sum).Sum)
        tracking_points = [double](
            ($runs | Measure-Object tracking_points -Sum).Sum)
        smooth_bonus = [double](
            ($runs | Measure-Object smooth_bonus -Sum).Sum)
        mean_error_px = [double](
            ($runs | Measure-Object mean_error_px -Average).Average)
        p95_error_px = [double](
            ($runs | Measure-Object p95_error_px -Average).Average)
        cue_continuation_frames = [int64](
            ($runs | Measure-Object cue_continuation_frames -Sum).Sum)
    }
}

$summary = [ordered]@{
    schema = "bodylock-pov-fire-matrix-v1"
    revision = $revision
    dirty = $dirty
    seeds = $Seeds
    duration_ms = $DurationMs
    fixed_target_slot_ms = 1575
    controller_hz = 1000
    plant_source = "historical_synthetic_default"
    short_occlusion_ms = 48
    short_occlusion_evidence = "same-generation-cue"
    comparisons_executed = [bool]$BaselineDir
    rows = $summaries
}
$summaryPath = Join-Path $OutputDir "summary.json"
[IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 6),
    [Text.UTF8Encoding]::new($false))

$summaries | Format-Table -AutoSize
Write-Output "BODYLOCK_POV_FIRE_MATRIX=BENCHMARK-ELIGIBLE"
Write-Output "Matrix written to $OutputDir"
