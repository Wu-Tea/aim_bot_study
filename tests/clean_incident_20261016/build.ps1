param([string]$ProductRoot = 'D:/work/AI/yolo-study-001-clean', [string]$BuildName = 'baseline', [string]$SourceRoot = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$devRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildPath = Join-Path $devRoot "runs/clean-incident-fix-20261016/$BuildName"
$cmakePath = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$vsPath = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat'
$commandText = 'call "' + $vsPath + '" -arch=x64 -host_arch=x64 >nul && "' + $cmakePath + '" -S "' + $SourceRoot + '" -B "' + $buildPath + '" -G "Visual Studio 17 2022" -A x64 -DPRODUCT_ROOT="' + $ProductRoot + '" && "' + $cmakePath + '" --build "' + $buildPath + '" --config Release -- /m:2 /nr:false'
python (Join-Path $PSScriptRoot 'run_env.py') cmd.exe /d /s /c $commandText
if ($LASTEXITCODE -ne 0) { throw "Checks build failed: $LASTEXITCODE" }
