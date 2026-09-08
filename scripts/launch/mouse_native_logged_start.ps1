param([Parameter(ValueFromRemainingArguments = $true)][string[]]$RuntimeArguments)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location -LiteralPath $projectRoot
$runtimePath = Join-Path $projectRoot 'native/vision_native/build/Release/cod_native_mouse_runtime.exe'
$configPath = $env:MOUSE_RUNTIME_CONFIG
if (-not $configPath) { $configPath = Join-Path $projectRoot 'config.toml' }
$transport = $env:MOUSE_RUNTIME_TRANSPORT
if (-not $transport) { $transport = 'virtual-hid' }
$logDirectory = Join-Path $projectRoot 'runs/mouse_startup'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$logPath = Join-Path $logDirectory ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N') + '.log')
@("started_at=$([DateTimeOffset]::Now.ToString('o'))", "transport_default=$transport") |
    Set-Content -LiteralPath $logPath -Encoding UTF8
Write-Output "Mouse startup log: $logPath"
$runtimeExit = 1
try {
    if (-not (Test-Path -LiteralPath $runtimePath)) { throw 'Native mouse runtime executable is missing.' }
    if (-not (Test-Path -LiteralPath $configPath)) { throw 'Mouse runtime configuration is missing.' }
    # Windows PowerShell represents native stderr as ErrorRecord objects.
    # Preserve the message and native exit status while streaming both channels.
    $ErrorActionPreference = 'Continue'
    & $runtimePath --config $configPath --transport $transport @RuntimeArguments 2>&1 |
        ForEach-Object {
            $line = $_.ToString()
            Add-Content -LiteralPath $logPath -Encoding UTF8 -Value $line -ErrorAction Stop
            Write-Output $line
        }
    $runtimeExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
} catch {
    Add-Content -LiteralPath $logPath -Encoding UTF8 -Value $_.Exception.Message
    Write-Output $_.Exception.Message
} finally {
    Add-Content -LiteralPath $logPath -Encoding UTF8 -Value "exit_code=$runtimeExit"
}
if ($runtimeExit -ne 0) {
    Write-Output "Mouse runtime failed with exit code $runtimeExit. See the startup log above."
}
exit $runtimeExit
