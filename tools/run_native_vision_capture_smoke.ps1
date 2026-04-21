param(
    [int]$Width = 640,
    [int]$Height = 512,
    [int]$AdapterIndex = 0,
    [int]$OutputIndex = -1,
    [int]$TimeoutMs = 50,
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
$ModuleDir = Join-Path $BuildDir $Configuration
$NativeModule = Get-ChildItem -Path $ModuleDir -Filter "vision_native_cpp*.pyd" -ErrorAction SilentlyContinue | Select-Object -First 1

if ($BuildFirst -or $null -eq $NativeModule) {
    & (Join-Path $PSScriptRoot "build_native_vision.ps1") `
        -TensorRTRoot $TensorRTRoot `
        -CudaPath $CudaPath `
        -BuildDir ($BuildDir.Substring($ProjectRoot.Length + 1)) `
        -Configuration $Configuration `
        -PythonExe $PythonExe
    $NativeModule = Get-ChildItem -Path $ModuleDir -Filter "vision_native_cpp*.pyd" -ErrorAction SilentlyContinue | Select-Object -First 1
}

if ($null -eq $NativeModule) {
    throw "vision_native_cpp module not found in: $ModuleDir"
}
if (-not (Test-Path $PythonExe)) {
    throw "Python executable not found: $PythonExe"
}

$env:CUDA_PATH = $CudaPath
$env:CudaToolkitDir = $CudaPath
$env:TensorRT_ROOT = $TensorRTRoot
$env:PATH = "$(Join-Path $TensorRTRoot 'bin');$(Join-Path $CudaPath 'bin');$env:PATH"
$env:PYTHONPATH = "$ModuleDir;$env:PYTHONPATH"

$Script = @"
import vision_native_cpp

capture = vision_native_cpp.NativeDxgiCapture(
    width=$Width,
    height=$Height,
    adapter_index=$AdapterIndex,
    output_index=$OutputIndex,
    timeout_ms=$TimeoutMs,
)
result = capture.grab()
frame = result["frame"]

print("native capture smoke ok")
print(f"output={result['output_width']}x{result['output_height']} roi={result['roi_left']},{result['roi_top']} {frame['width']}x{frame['height']}")
print(f"updated={result['updated']} memory_kind={result['memory_kind']} format={result['format']} has_data={frame['has_data']}")
print(f"acquire_ms={result['acquire_ms']:.3f} copy_ms={result['copy_ms']:.3f}")
"@

$Script | & $PythonExe -
if ($LASTEXITCODE -ne 0) {
    throw "native capture smoke failed with exit code $LASTEXITCODE"
}
