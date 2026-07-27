param(
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [string]$Config = "",
    [string]$BuildDir = "",
    [uint32[]]$Seeds = @(2026072701, 2026072702, 2026072703),
    [int]$DurationMs = 60000,
    [ValidateSet("pure", "mixed")][string]$Profile = "mixed",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Config) {
    $Config = Join-Path $root "..\..\config.toml"
}
$Config = (Resolve-Path $Config).Path
if (-not $BuildDir) {
    $BuildDir = Join-Path $root "native\vision_native\build-modern"
}
$exe = Join-Path $BuildDir "Release\cod_native_sustained_aimlab_benchmark.exe"
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (Test-Path $OutputDir) {
    if ((Get-ChildItem $OutputDir -Force | Measure-Object).Count -gt 0) {
        throw "Refusing to overwrite non-empty output directory: $OutputDir"
    }
} else {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

if (-not $SkipBuild) {
    & $cmake --build $BuildDir --config Release `
        --target cod_native_sustained_aimlab_benchmark -- /m:1
    if ($LASTEXITCODE -ne 0) { throw "benchmark build failed" }
}
if (-not (Test-Path $exe)) { throw "benchmark executable not found: $exe" }

$revision = (& git -C $root rev-parse HEAD).Trim()
& git -C $root diff --quiet
$dirty = $LASTEXITCODE -ne 0
$durations = @(0, 24, 36, 48)
$reports = @()

foreach ($occlusion in $durations) {
    $output = Join-Path $OutputDir "occlusion-$occlusion-ms.json"
    $arguments = @(
        "--config", $Config,
        "--duration-ms", [string]$DurationMs,
        "--profile", $Profile,
        "--cohort", "ads",
        "--target-profile", "ordinary",
        "--camera-response", "700",
        "--slowdown-edge", "0.80",
        "--slowdown-center", "0.70",
        "--short-occlusion-ms", [string]$occlusion,
        "--intent-fusion", "vector",
        "--counterfactual", "off",
        "--revision", $revision,
        "--output", $output
    )
    foreach ($seed in $Seeds) {
        $arguments += @("--seed", [string]$seed)
    }
    if ($dirty) { $arguments += "--dirty" }

    & $exe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "benchmark failed for $occlusion ms occlusion"
    }
    $reports += Get-Content -Raw $output | ConvertFrom-Json
}

$fingerprints = @($reports | ForEach-Object {
    $_.config_fingerprint_fnv1a64
} | Sort-Object -Unique)
if ($fingerprints.Count -ne 1) {
    throw "matrix used more than one runtime config fingerprint"
}

$rows = foreach ($report in $reports) {
    $runs = @($report.runs)
    [pscustomobject]@{
        short_occlusion_ms = [int]$report.simulator.short_occlusion_ms
        script_hashes = @($runs | ForEach-Object { $_.script_hash })
        tracking_points = [double](($runs | Measure-Object tracking_points -Sum).Sum)
        mean_error_px = [double](($runs | Measure-Object mean_error_px -Average).Average)
        circle_exit_events = [int](($runs | Measure-Object circle_exit_events -Sum).Sum)
        direction_discontinuities = [int](($runs | Measure-Object direction_discontinuities -Sum).Sum)
        occluded_direction_discontinuities = [int](($runs | Measure-Object occluded_direction_discontinuities -Sum).Sum)
        fresh_vision_direction_discontinuities = [int](($runs | Measure-Object fresh_vision_direction_discontinuities -Sum).Sum)
        manual_input_discontinuities = [int](($runs | Measure-Object manual_input_discontinuities -Sum).Sum)
        manual_driven_final_discontinuities = [int](($runs | Measure-Object manual_driven_final_discontinuities -Sum).Sum)
        controller_residual_discontinuities = [int](($runs | Measure-Object controller_residual_discontinuities -Sum).Sum)
        controller_residual_kick_events = [int](($runs | Measure-Object controller_residual_kick_events -Sum).Sum)
        requested_assist_discontinuities = [int](($runs | Measure-Object requested_assist_discontinuities -Sum).Sum)
        shaped_assist_discontinuities = [int](($runs | Measure-Object shaped_assist_discontinuities -Sum).Sum)
        p95_jerk = [double](($runs | Measure-Object p95_jerk -Average).Average)
        p95_controller_residual_delta = [double](($runs | Measure-Object p95_controller_residual_delta -Average).Average)
        p95_controller_residual_jerk = [double](($runs | Measure-Object p95_controller_residual_jerk -Average).Average)
        p99_controller_residual_delta = [double](($runs | Measure-Object p99_controller_residual_delta -Average).Average)
        max_controller_residual_delta = [double](($runs | Measure-Object max_controller_residual_delta -Maximum).Maximum)
        occlusion_episodes = [int](($runs | Measure-Object occlusion_episodes -Sum).Sum)
        post_occlusion_error_area_px_ms = [double](($runs | Measure-Object post_occlusion_error_area_px_ms -Sum).Sum)
        max_post_occlusion_error_px = [double](($runs | Measure-Object max_post_occlusion_error_px -Maximum).Maximum)
        p95_reveal_to_stable_ms = [double](($runs | Measure-Object p95_reveal_to_stable_ms -Maximum).Maximum)
    }
}

$summary = [ordered]@{
    schema = "weak-slowdown-occlusion-matrix-v1"
    revision = $revision
    dirty = $dirty
    config_fingerprint_fnv1a64 = $fingerprints[0]
    seeds = $Seeds
    duration_ms = $DurationMs
    profile = $Profile
    camera_response = 700
    slowdown_edge = 0.80
    slowdown_center = 0.70
    rows = @($rows)
}
$summaryPath = Join-Path $OutputDir "summary.json"
[IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 6),
    [Text.UTF8Encoding]::new($false))

Write-Output "Weak-slowdown occlusion matrix written to $OutputDir"
