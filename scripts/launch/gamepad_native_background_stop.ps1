param(
    [ValidateSet('default', 'apex', 'bo3')][string]$Game = 'default',
    [switch]$PrintOnly
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$pythonPath = 'D:\env\python\python.exe'
$action = if ($PrintOnly) { 'preview-stop' } else { 'stop' }
$env:PYTHONPATH = (Join-Path $projectRoot 'python') + [IO.Path]::PathSeparator + $env:PYTHONPATH
Push-Location $projectRoot
try {
    & $pythonPath -m desktop_app.gui --action $action --game $Game
    exit $LASTEXITCODE
} finally { Pop-Location }
