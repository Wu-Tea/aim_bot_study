param(
    [Parameter(Mandatory = $true)][string[]]$Configs,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [int]$DurationMs = 8000,
    [uint32[]]$Seeds = @(1337),
    [int[]]$CameraResponses = @(500),
    [string[]]$Slowdowns = @("light:0.65:0.55", "default:0.50:0.40", "strong:0.40:0.30"),
    [ValidateSet("ordinary", "small")][string[]]$TargetProfiles = @("ordinary", "small"),
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$exe = Join-Path $root "b\Release\cod_native_sustained_aimlab_benchmark.exe"

if (-not $SkipBuild) {
    & $cmake --build (Join-Path $root "b") --config Release `
        --target cod_native_sustained_aimlab_benchmark
    if ($LASTEXITCODE -ne 0) { throw "benchmark build failed" }
}
if (-not (Test-Path $exe)) { throw "benchmark executable not found: $exe" }

$namedConfigs = foreach ($entry in $Configs) {
    $parts = $entry.Split("=", 2)
    if ($parts.Count -ne 2 -or -not $parts[0] -or -not $parts[1]) {
        throw "Config must use name=path syntax: $entry"
    }
    [pscustomobject]@{ Name = $parts[0]; Path = (Resolve-Path $parts[1]).Path }
}
$parsedSlowdowns = foreach ($entry in $Slowdowns) {
    $parts = $entry.Split(":")
    if ($parts.Count -ne 3) { throw "Slowdown must use label:edge:center syntax: $entry" }
    [pscustomobject]@{ Label = $parts[0]; Edge = $parts[1]; Center = $parts[2] }
}

New-Item -ItemType Directory -Force $OutputDir | Out-Null
$revision = (& git -C $root rev-parse HEAD).Trim()
foreach ($config in $namedConfigs) {
    foreach ($slowdown in $parsedSlowdowns) {
        foreach ($camera in $CameraResponses) {
            foreach ($targetProfile in $TargetProfiles) {
                $output = Join-Path $OutputDir `
                    "$($config.Name)-$($slowdown.Label)-c$camera-$targetProfile.json"
                if (Test-Path $output) { throw "Refusing to overwrite result: $output" }
                $arguments = @(
                    "--config", $config.Path,
                    "--profile", "both", "--cohort", "both",
                    "--target-profile", $targetProfile,
                    "--camera-response", $camera,
                    "--slowdown-edge", $slowdown.Edge,
                    "--slowdown-center", $slowdown.Center,
                    "--duration-ms", $DurationMs,
                    "--revision", $revision,
                    "--output", $output
                )
                foreach ($seed in $Seeds) { $arguments += @("--seed", $seed) }
                & $exe @arguments
                if ($LASTEXITCODE -ne 0) {
                    throw "benchmark failed: $($config.Name) $($slowdown.Label) c$camera $targetProfile"
                }
            }
        }
    }
}

Write-Output "Brake sweep complete: $OutputDir"
