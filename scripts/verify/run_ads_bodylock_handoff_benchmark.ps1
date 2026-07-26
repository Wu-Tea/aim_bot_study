param(
    [Parameter(Mandatory = $true)][string]$BaseConfig,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [string]$BuildDir = "D:\b\cmix",
    [uint32[]]$Seeds = @(2026072601, 2026072602, 2026072603),
    [int]$DurationMs = 60000,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$BaseConfig = (Resolve-Path $BaseConfig).Path
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
    & $cmake --build $BuildDir --config Release --target cod_native_sustained_aimlab_benchmark
    if ($LASTEXITCODE -ne 0) { throw "benchmark build failed" }
}
if (-not (Test-Path $exe)) { throw "benchmark executable not found: $exe" }

$source = [IO.File]::ReadAllText($BaseConfig)
function New-VariantConfig([string]$Name, [int]$Activation, [int]$Tolerance) {
    $text = [regex]::Replace(
        $source, '(?m)^activation_range_px\s*=\s*\d+\s*$',
        "activation_range_px = $Activation")
    $text = [regex]::Replace(
        $text, '(?m)^tolerance_px\s*=\s*\d+\s*$',
        "tolerance_px = $Tolerance")
    $path = Join-Path $OutputDir "$Name.toml"
    [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
    return $path
}

$variants = @(
    [pscustomobject]@{
        Name = "baseline_80_8"
        Config = (New-VariantConfig "baseline_80_8" 80 8)
    },
    [pscustomobject]@{
        Name = "live_120_16"
        Config = (New-VariantConfig "live_120_16" 120 16)
    }
)
$slowdowns = @(
    [pscustomobject]@{ Name = "default"; Edge = 0.50; Center = 0.40 },
    [pscustomobject]@{ Name = "strong"; Edge = 0.40; Center = 0.30 }
)

$revision = (& git -C $root rev-parse HEAD).Trim()
& git -C $root diff --quiet
$dirty = $LASTEXITCODE -ne 0
foreach ($variant in $variants) {
    foreach ($slowdown in $slowdowns) {
        $output = Join-Path $OutputDir "$($variant.Name)_$($slowdown.Name).json"
        $arguments = @(
            "--config", $variant.Config,
            "--duration-ms", $DurationMs,
            "--profile", "mixed",
            "--cohort", "ads",
            "--target-profile", "ordinary",
            "--camera-response", "500",
            "--slowdown-edge", ([string]$slowdown.Edge),
            "--slowdown-center", ([string]$slowdown.Center),
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
            throw "benchmark failed: $($variant.Name)/$($slowdown.Name)"
        }
    }
}

Write-Output "ADS->BodyLock handoff matrix written to $OutputDir"
