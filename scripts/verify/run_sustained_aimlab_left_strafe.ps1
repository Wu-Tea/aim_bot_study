param(
    [string]$Config = "",
    [string]$Output = "",
    [int]$DurationMs = 60000,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$commonGitDir = (& git -C $root rev-parse --path-format=absolute --git-common-dir).Trim()
$primaryRoot = Split-Path $commonGitDir -Parent

if (-not $Config) {
    $liveConfig = Join-Path $primaryRoot "config.toml"
    $Config = if (Test-Path -LiteralPath $liveConfig) {
        $liveConfig
    } else {
        Join-Path $root "config.native.example.toml"
    }
}
$Config = (Resolve-Path -LiteralPath $Config).Path
if (-not $Output) {
    $Output = Join-Path $root (
        "artifacts\benchmarks\sustained_aimlab\left-strafe-20260723.json")
}
if (Test-Path -LiteralPath $Output) {
    throw "Refusing to overwrite retained benchmark: $Output"
}

$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$build = Join-Path $root "b"
if (-not $SkipBuild) {
    & $cmake --build $build --config Release `
        --target cod_native_sustained_aimlab_benchmark
    if ($LASTEXITCODE -ne 0) { throw "benchmark build failed" }
}

$revision = (& git -C $root rev-parse HEAD).Trim()
& git -C $root diff --quiet
$dirty = $LASTEXITCODE -ne 0
$arguments = @(
    "--config", $Config,
    "--duration-ms", $DurationMs,
    "--seed", "2026072301",
    "--seed", "2026072302",
    "--seed", "2026072303",
    "--profile", "both",
    "--cohort", "both",
    "--intent-fusion", "vector",
    "--left-strafe", "both",
    "--revision", $revision,
    "--output", $Output
)
if ($dirty) { $arguments += "--dirty" }

& (Join-Path $build "Release\cod_native_sustained_aimlab_benchmark.exe") `
    @arguments
if ($LASTEXITCODE -ne 0) { throw "benchmark execution failed" }

$report = Get-Content -Raw -LiteralPath $Output | ConvertFrom-Json
if ($report.schema -ne "sustained-aimlab-v2") {
    throw "unexpected benchmark schema: $($report.schema)"
}
if ($report.runs.Count -ne 24) {
    throw "expected 24 paired runs, got $($report.runs.Count)"
}
$groups = $report.runs | Group-Object seed, profile, cohort
if ($groups.Count -ne 12) {
    throw "expected 12 seed/profile/cohort pairs, got $($groups.Count)"
}
foreach ($group in $groups) {
    if ($group.Count -ne 2) {
        throw "pair $($group.Name) does not contain two strafe modes"
    }
    $off = $group.Group | Where-Object left_strafe -eq "off"
    $strafe = $group.Group | Where-Object left_strafe -eq "full_reversal"
    if ($null -eq $off -or $null -eq $strafe) {
        throw "pair $($group.Name) lost off/full_reversal identity"
    }
    if ($off.script_hash -ne $strafe.script_hash) {
        throw "pair $($group.Name) does not share script hash"
    }
    if ($off.max_abs_left_x -ne 0.0 -or
        $off.max_abs_player_speed_px_per_second -ne 0.0) {
        throw "off pair $($group.Name) contains player motion"
    }
    if ($strafe.max_abs_left_x -ne 1.0 -or
        $strafe.max_abs_player_speed_px_per_second -le 0.0) {
        throw "strafe pair $($group.Name) lost full-speed player motion"
    }
}

Write-Output "Paired left-strafe benchmark written to $Output"
