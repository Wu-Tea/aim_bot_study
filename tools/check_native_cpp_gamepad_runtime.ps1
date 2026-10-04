param(
    [switch]$BuildFirst,
    [string]$BuildDirectory = "native\build",
    [string]$RuntimeConfig = "config.toml"
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($BuildFirst) { & (Join-Path $PSScriptRoot 'build_native_runtime.ps1') -BuildDir $BuildDirectory }
$buildRoot = Join-Path $projectRoot $BuildDirectory
$runtime = Join-Path $buildRoot 'Release\cod_native_runtime.exe'
& $runtime --config (Join-Path $projectRoot $RuntimeConfig) --dump-effective-config
if ($LASTEXITCODE -ne 0) { throw 'Native configuration check failed.' }
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $cmake --test-dir $buildRoot -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
