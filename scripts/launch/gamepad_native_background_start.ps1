param(
    [switch]$PrintOnly
)

$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$executablePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "native\vision_native\build\Release\cod_native_runtime.exe"))
$configPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "config.toml"))
$stateDirectory = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "runs\runtime\background"))
$statePath = Join-Path $stateDirectory "native_runtime_state.json"
$stdoutPath = Join-Path $stateDirectory "native_runtime.stdout.log"
$stderrPath = Join-Path $stateDirectory "native_runtime.stderr.log"
$launcherLogPath = Join-Path $stateDirectory "launcher.log"

if ($PrintOnly) {
    [ordered]@{
        action = "preview_start"
        executable_path = $executablePath
        config_path = $configPath
        state_path = $statePath
        stdout_path = $stdoutPath
        stderr_path = $stderrPath
    } | ConvertTo-Json -Compress
    exit 0
}

New-Item -ItemType Directory -Force -Path $stateDirectory | Out-Null

function Write-LauncherLog([string]$Message) {
    $timestamp = [DateTime]::UtcNow.ToString("o")
    Add-Content -LiteralPath $launcherLogPath -Encoding UTF8 -Value "$timestamp start $Message"
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
    if (Test-Path -LiteralPath $statePath) {
        try {
            $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
            $recordedProcessId = [int]$state.process_id
            $record = Get-ProcessRecord $recordedProcessId
            if ($null -ne $record -and
                (Paths-Equal $record.ExecutablePath $state.executable_path) -and
                (Paths-Equal $record.ExecutablePath $executablePath)) {
                Write-LauncherLog "already_running pid=$recordedProcessId"
                exit 0
            }
        } catch {
            Write-LauncherLog "discarding_invalid_state error=$($_.Exception.Message)"
        }
        Remove-Item -LiteralPath $statePath -Force -ErrorAction SilentlyContinue
    }

    if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "Native runtime executable not found: $executablePath"
    }
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        throw "Runtime config not found: $configPath"
    }

    $sameExecutable = Get-CimInstance -ClassName Win32_Process `
        -Filter "Name = 'cod_native_runtime.exe'" `
        -ErrorAction SilentlyContinue | Where-Object {
            Paths-Equal $_.ExecutablePath $executablePath
        }
    if ($null -ne $sameExecutable) {
        Write-LauncherLog "refused_unowned_duplicate"
        exit 3
    }

    $process = Start-Process `
        -FilePath $executablePath `
        -ArgumentList @("--config", ('"{0}"' -f $configPath)) `
        -WorkingDirectory $projectRoot `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru

    $state = [ordered]@{
        process_id = $process.Id
        executable_path = $executablePath
        config_path = $configPath
        started_at_utc = [DateTime]::UtcNow.ToString("o")
    }
    $temporaryStatePath = "$statePath.tmp.$PID"
    $state | ConvertTo-Json | Set-Content -LiteralPath $temporaryStatePath -Encoding UTF8
    Move-Item -LiteralPath $temporaryStatePath -Destination $statePath -Force
    Write-LauncherLog "started pid=$($process.Id)"
    exit 0
} catch {
    Write-LauncherLog "failed error=$($_.Exception.Message)"
    exit 1
}
