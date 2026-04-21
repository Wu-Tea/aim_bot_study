param(
    [string]$EnginePath = "models\best.engine",
    [string]$TensorRTRoot = "D:\env\TensorRT-10.15.1.29",
    [string]$CudaPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1",
    [string]$BuildDir = "native\vision_native\build",
    [string]$Configuration = "Release",
    [switch]$BuildFirst
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$EnginePath = Join-Path $ProjectRoot $EnginePath
$BuildDir = Join-Path $ProjectRoot $BuildDir
$SmokeExe = Join-Path $BuildDir "$Configuration\vision_native_smoke.exe"

if ($BuildFirst -or -not (Test-Path $SmokeExe)) {
    & (Join-Path $PSScriptRoot "build_native_vision.ps1") `
        -TensorRTRoot $TensorRTRoot `
        -CudaPath $CudaPath `
        -BuildDir ($BuildDir.Substring($ProjectRoot.Length + 1)) `
        -Configuration $Configuration
}

if (-not (Test-Path $SmokeExe)) {
    throw "vision_native_smoke.exe not found: $SmokeExe"
}
if (-not (Test-Path $EnginePath)) {
    throw "engine file not found: $EnginePath"
}

$env:CUDA_PATH = $CudaPath
$env:TensorRT_ROOT = $TensorRTRoot
$env:PATH = "$(Join-Path $TensorRTRoot 'bin');$(Join-Path $CudaPath 'bin');$env:PATH"

& $SmokeExe $EnginePath
if ($LASTEXITCODE -ne 0) {
    throw "vision_native_smoke.exe failed with exit code $LASTEXITCODE"
}
