param(
    [string]$TensorRTRoot = "D:\env\TensorRT-10.15.1.29",
    [string]$CudaPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1",
    [string]$BuildDir = "native\vision_native\build",
    [string]$Configuration = "Release",
    [string]$PythonExe = "D:\env\python\python.exe",
    [string]$Pybind11CMakeDir = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$NativeSourceDir = Join-Path $ProjectRoot "native\vision_native"
$BuildDir = Join-Path $ProjectRoot $BuildDir
$VsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
$CMakeExe = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (-not (Test-Path $VsDevCmd)) {
    throw "VS Dev Cmd not found: $VsDevCmd"
}
if (-not (Test-Path $CMakeExe)) {
    throw "CMake not found: $CMakeExe"
}
if (-not (Test-Path (Join-Path $TensorRTRoot "include\NvInfer.h"))) {
    throw "TensorRT SDK not found or incomplete: $TensorRTRoot"
}
if (-not (Test-Path (Join-Path $CudaPath "include\cuda.h"))) {
    throw "CUDA Toolkit not found or incomplete: $CudaPath"
}
if (-not (Test-Path $PythonExe)) {
    throw "Python executable not found: $PythonExe"
}

if ([string]::IsNullOrWhiteSpace($Pybind11CMakeDir)) {
    $Pybind11CMakeDir = & $PythonExe -m pybind11 --cmakedir
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($Pybind11CMakeDir)) {
        throw "Failed to locate pybind11 CMake directory with: $PythonExe -m pybind11 --cmakedir"
    }
    $Pybind11CMakeDir = $Pybind11CMakeDir.Trim()
}

$env:CUDA_PATH = $CudaPath
$env:TensorRT_ROOT = $TensorRTRoot
$env:PATH = "$(Join-Path $TensorRTRoot 'bin');$(Join-Path $CudaPath 'bin');$env:PATH"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$ConfigureArgs = @(
    "-S", $NativeSourceDir,
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DTensorRT_ROOT=$TensorRTRoot",
    "-DCUDAToolkit_ROOT=$CudaPath",
    "-Dpybind11_DIR=$Pybind11CMakeDir",
    "-DPython_EXECUTABLE=$PythonExe",
    "-DPython3_EXECUTABLE=$PythonExe"
)

$BuildArgs = @(
    "--build", $BuildDir,
    "--config", $Configuration
)

$QuotedConfigureArgs = ($ConfigureArgs | ForEach-Object { "`"$_`"" }) -join " "
$QuotedBuildArgs = ($BuildArgs | ForEach-Object { "`"$_`"" }) -join " "
$ConfigureCommand = "`"$CMakeExe`" $QuotedConfigureArgs"
$BuildCommand = "`"$CMakeExe`" $QuotedBuildArgs"
$Cmd = "call `"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && $ConfigureCommand && $BuildCommand"

cmd.exe /s /c $Cmd
if ($LASTEXITCODE -ne 0) {
    throw "native vision build failed with exit code $LASTEXITCODE"
}

Write-Host "Native vision build completed: $BuildDir\$Configuration"
