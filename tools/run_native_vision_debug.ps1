param(
    [int]$Width = 640,
    [int]$Height = 512,
    [int]$Frames = 8,
    [int]$TimeoutMs = 10,
    [switch]$Aim = $true,
    [string]$TensorRTRoot = "D:\env\TensorRT-10.15.1.29",
    [string]$CudaPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1",
    [string]$BuildDir = "native\vision_native\build",
    [string]$Configuration = "Release",
    [string]$PythonExe = "D:\env\python\python.exe",
    [switch]$BuildFirst
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot $BuildDir
$ExeDir = Join-Path $BuildDir $Configuration
$DebugExe = Join-Path $ExeDir "vision_native_debug.exe"

if ($BuildFirst -or -not (Test-Path $DebugExe)) {
    & (Join-Path $PSScriptRoot "build_native_vision.ps1") `
        -TensorRTRoot $TensorRTRoot `
        -CudaPath $CudaPath `
        -BuildDir ($BuildDir.Substring($ProjectRoot.Length + 1)) `
        -Configuration $Configuration `
        -PythonExe $PythonExe
}

if (-not (Test-Path $DebugExe)) {
    throw "vision_native_debug.exe not found in: $ExeDir"
}

$env:CUDA_PATH = $CudaPath
$env:CudaToolkitDir = $CudaPath
$env:TensorRT_ROOT = $TensorRTRoot
$env:PATH = "$(Join-Path $TensorRTRoot 'bin');$(Join-Path $CudaPath 'bin');$env:PATH"

$Args = @(
    "--width", $Width,
    "--height", $Height,
    "--frames", $Frames,
    "--timeout-ms", $TimeoutMs
)
if ($Aim) {
    $Args += "--aim"
}
else {
    $Args += "--no-aim"
}

& $DebugExe @Args
if ($LASTEXITCODE -ne 0) {
    throw "vision native debug failed with exit code $LASTEXITCODE"
}
