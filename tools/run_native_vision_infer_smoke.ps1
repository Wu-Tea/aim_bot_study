param(
    [string]$EnginePath = "models\best.engine",
    [string]$TensorRTRoot = "D:\env\TensorRT-10.15.1.29",
    [string]$CudaPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1",
    [string]$BuildDir = "native\vision_native\build",
    [string]$Configuration = "Release",
    [string]$PythonExe = "D:\env\python\python.exe",
    [switch]$BuildFirst
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$EnginePath = Join-Path $ProjectRoot $EnginePath
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
if (-not (Test-Path $EnginePath)) {
    throw "engine file not found: $EnginePath"
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
import numpy as np
import vision_native_cpp

engine = vision_native_cpp.NativeEngine(r"$EnginePath")
frame = np.zeros((engine.input_height, engine.input_width, 3), dtype=np.uint8)
result = engine.infer_rgb(frame, 0.4)

print("native inference smoke ok")
print(f"input={engine.input_width}x{engine.input_height} output={engine.output_rows}x{engine.output_cols}")
print(f"boxes={result['boxes_seen']} preprocess_ms={result['preprocess_ms']:.3f} infer_ms={result['infer_ms']:.3f} decode_ms={result['decode_ms']:.3f}")
"@

$Script | & $PythonExe -
if ($LASTEXITCODE -ne 0) {
    throw "native inference smoke failed with exit code $LASTEXITCODE"
}
