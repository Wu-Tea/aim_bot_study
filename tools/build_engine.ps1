param(
    [Parameter(Mandatory = $true)][string]$OnnxPath,
    [Parameter(Mandatory = $true)][string]$EnginePath,
    [string]$TensorRTRoot = 'D:\env\TensorRT-10.15.1.29',
    [string]$InputShape = ''
)
$ErrorActionPreference = 'Stop'
$builder = Join-Path $TensorRTRoot 'bin/trtexec.exe'
if (-not (Test-Path -LiteralPath $builder -PathType Leaf)) { throw 'TensorRT trtexec.exe is missing.' }
$model = (Resolve-Path -LiteralPath $OnnxPath).Path
$engine = [IO.Path]::GetFullPath($EnginePath)
if (Test-Path -LiteralPath $engine) { throw 'Choose a new engine path; the existing engine will be preserved.' }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $engine) | Out-Null
$arguments = @("--onnx=$model", "--saveEngine=$engine")
if (-not [string]::IsNullOrWhiteSpace($InputShape)) { $arguments += "--shapes=$InputShape" }
& $builder @arguments
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $engine -PathType Leaf)) {
    throw 'TensorRT engine build failed; inspect the SDK output.'
}
