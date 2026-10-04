param(
    [ValidateSet('default', 'apex', 'bo3')][string]$Game = 'default',
    [switch]$PrintOnly
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$assistant = Join-Path $projectRoot 'native\build\Release\cod_native_assistant.exe'
if (-not (Test-Path -LiteralPath $assistant)) { throw 'Build tools/build_native_runtime.ps1 first.' }
$env:FUSION_ENABLED = '1'
$action = if ($PrintOnly) { 'preview-start' } else { 'start' }
& $assistant --project $projectRoot --action $action --game $Game
exit $LASTEXITCODE
