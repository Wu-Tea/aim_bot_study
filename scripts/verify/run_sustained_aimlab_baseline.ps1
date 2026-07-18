param(
    [string]$Config = "",
    [string]$Output = "",
    [int]$DurationMs = 60000,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Config) {
    $Config = Join-Path (Split-Path $root -Parent | Split-Path -Parent) "config.toml"
}
$Config = (Resolve-Path $Config).Path
if (-not $Output) {
    $Output = Join-Path $root "artifacts\benchmarks\sustained_aimlab\baseline-20260718.json"
}
if (Test-Path $Output) {
    throw "Refusing to overwrite baseline: $Output"
}

$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$build = Join-Path $root "b"
if (-not $SkipBuild) {
    & $cmake --build $build --config Release --target cod_native_sustained_aimlab_benchmark
    if ($LASTEXITCODE -ne 0) { throw "benchmark build failed" }
}

$revision = (& git -C $root rev-parse HEAD).Trim()
& git -C $root diff --quiet
$dirty = $LASTEXITCODE -ne 0
$args = @(
    "--config", $Config,
    "--duration-ms", $DurationMs,
    "--seed", "1337",
    "--seed", "20260718",
    "--seed", "424242",
    "--profile", "both",
    "--revision", $revision,
    "--output", $Output
)
if ($dirty) { $args += "--dirty" }

& (Join-Path $build "Release\cod_native_sustained_aimlab_benchmark.exe") @args
if ($LASTEXITCODE -ne 0) { throw "benchmark execution failed" }
Write-Output "Baseline written to $Output"
