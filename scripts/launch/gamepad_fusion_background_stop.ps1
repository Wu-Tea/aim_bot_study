param(
    [switch]$PrintOnly
)

$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$executablePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "native\vision_native\build\Release\fusion_canvas.exe"))
$stateDirectory = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "runs\fusion_canvas\background"))
$statePath = Join-Path $stateDirectory "fusion_canvas_state.json"
$launcherLogPath = Join-Path $stateDirectory "launcher.log"

if ($PrintOnly) {
    [ordered]@{
        action = "preview_fusion_stop"
        state_path = $statePath
    } | ConvertTo-Json -Compress
    exit 0
}

New-Item -ItemType Directory -Force -Path $stateDirectory | Out-Null

function Write-LauncherLog([string]$Message) {
    $timestamp = [DateTime]::UtcNow.ToString("o")
    Add-Content -LiteralPath $launcherLogPath -Encoding UTF8 -Value "$timestamp stop $Message"
}

function Get-ProcessRecord([int]$ProcessId) {
    Get-CimInstance -ClassName Win32_Process `
        -Filter ("ProcessId = {0}" -f $ProcessId) `
        -ErrorAction SilentlyContinue
}

function Paths-Equal([string]$Left, [string]$Right) {
    if ([string]::IsNullOrWhiteSpace($Left) -or [string]::IsNullOrWhiteSpace($Right)) {
        return $false
    }
    return [string]::Equals(
        [System.IO.Path]::GetFullPath($Left),
        [System.IO.Path]::GetFullPath($Right),
        [System.StringComparison]::OrdinalIgnoreCase)
}

try {
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        Write-LauncherLog "not_running"
        exit 0
    }

    try {
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        $recordedProcessId = [int]$state.process_id
        $recordedExecutablePath = [string]$state.executable_path
    } catch {
        Remove-Item -LiteralPath $statePath -Force -ErrorAction SilentlyContinue
        Write-LauncherLog "discarded_invalid_state error=$($_.Exception.Message)"
        exit 0
    }

    $record = Get-ProcessRecord $recordedProcessId
    if ($null -eq $record) {
        Remove-Item -LiteralPath $statePath -Force -ErrorAction SilentlyContinue
        Write-LauncherLog "discarded_dead_state pid=$recordedProcessId"
        exit 0
    }
    if (-not (Paths-Equal $recordedExecutablePath $executablePath) -or
        -not (Paths-Equal $record.ExecutablePath $recordedExecutablePath) -or
        -not (Paths-Equal $record.ExecutablePath $executablePath)) {
        Remove-Item -LiteralPath $statePath -Force -ErrorAction SilentlyContinue
        Write-LauncherLog "refused_path_mismatch pid=$recordedProcessId"
        exit 2
    }

    Stop-Process -Id $recordedProcessId -Force -ErrorAction Stop
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        if ($null -eq (Get-ProcessRecord $recordedProcessId)) {
            break
        }
        Start-Sleep -Milliseconds 50
    }
    Remove-Item -LiteralPath $statePath -Force -ErrorAction SilentlyContinue
    Write-LauncherLog "stopped pid=$recordedProcessId"
    exit 0
} catch {
    Write-LauncherLog "failed error=$($_.Exception.Message)"
    exit 1
}
