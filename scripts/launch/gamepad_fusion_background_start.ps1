param(
    [switch]$PrintOnly
)

$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$executablePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "native\vision_native\build\Release\fusion_canvas.exe"))
$nativeExecutablePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "native\vision_native\build\Release\cod_native_runtime.exe"))
$nativeStartScript = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "gamepad_native_background_start.ps1"))
$powerShellPath = [System.IO.Path]::GetFullPath((Join-Path $PSHOME "powershell.exe"))
$nativeStatePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "runs\runtime\background\native_runtime_state.json"))
$stateDirectory = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "runs\fusion_canvas\background"))
$statePath = Join-Path $stateDirectory "fusion_canvas_state.json"
$stdoutPath = Join-Path $stateDirectory "fusion_canvas.stdout.log"
$stderrPath = Join-Path $stateDirectory "fusion_canvas.stderr.log"
$preflightStdoutPath = Join-Path $stateDirectory "capture_preflight.stdout.log"
$preflightStderrPath = Join-Path $stateDirectory "capture_preflight.stderr.log"
$runToken = "{0}-{1}" -f [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmssfff"), $PID
$preflightLogPath = Join-Path $stateDirectory "capture_preflight.$runToken.log"
$canvasLogPath = Join-Path $stateDirectory "fusion_canvas.$runToken.log"
$launcherLogPath = Join-Path $stateDirectory "launcher.log"
$session = if ([string]::IsNullOrWhiteSpace($env:FUSION_SESSION)) {
    "dev"
} else {
    $env:FUSION_SESSION
}
$maxFps = 30
if (-not [string]::IsNullOrWhiteSpace($env:FUSION_MAX_FPS)) {
    $parsedMaxFps = 0
    if ([int]::TryParse($env:FUSION_MAX_FPS, [ref]$parsedMaxFps) -and $parsedMaxFps -gt 0) {
        $maxFps = $parsedMaxFps
    }
}
$idleMode = if ([string]::IsNullOrWhiteSpace($env:FUSION_IDLE_MODE)) {
    "hide"
} else {
    $env:FUSION_IDLE_MODE
}

if ($PrintOnly) {
    [ordered]@{
        action = "preview_fusion_start"
        executable_path = $executablePath
        native_state_path = $nativeStatePath
        native_start_script = $nativeStartScript
        auto_start_native = $true
        state_path = $statePath
        session = $session
        max_fps = $maxFps
        idle_mode = $idleMode
        canvas_log_path = $canvasLogPath
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

function Get-OwnedNativeState {
    if (-not (Test-Path -LiteralPath $nativeStatePath -PathType Leaf)) {
        return $null
    }
    try {
        $state = Get-Content -LiteralPath $nativeStatePath -Raw | ConvertFrom-Json
        $recordedProcessId = [int]$state.process_id
        $record = Get-ProcessRecord $recordedProcessId
        if ($null -eq $record -or
            -not (Paths-Equal $record.ExecutablePath $state.executable_path) -or
            -not (Paths-Equal $record.ExecutablePath $nativeExecutablePath)) {
            return $null
        }
        return $state
    } catch {
        Write-LauncherLog "native_state_invalid error=$($_.Exception.Message)"
        return $null
    }
}

$canvasProcess = $null
$nativeStartedByFusion = $false
$ownedCanvasRunning = $false
$ownedCanvasState = $null
$ownedCanvasProcessId = 0
try {
    if (Test-Path -LiteralPath $statePath) {
        try {
            $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
            $recordedProcessId = [int]$state.process_id
            $record = Get-ProcessRecord $recordedProcessId
            if ($null -ne $record -and
                (Paths-Equal $record.ExecutablePath $state.executable_path) -and
                (Paths-Equal $record.ExecutablePath $executablePath)) {
                $ownedCanvasRunning = $true
                $ownedCanvasState = $state
                $ownedCanvasProcessId = $recordedProcessId
            }
        } catch {
            Write-LauncherLog "discarding_invalid_state error=$($_.Exception.Message)"
        }
        if (-not $ownedCanvasRunning) {
            Remove-Item -LiteralPath $statePath -Force -ErrorAction SilentlyContinue
        }
    }

    if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "Fusion canvas executable not found: $executablePath"
    }

    if ($ownedCanvasRunning) {
        $canvasSession = [string]$ownedCanvasState.session
        if ([string]::IsNullOrWhiteSpace($canvasSession)) {
            throw "Owned Fusion canvas state does not contain a session"
        }
        $session = $canvasSession
    }

    if (-not $ownedCanvasRunning) {
        $sameExecutable = Get-CimInstance -ClassName Win32_Process `
            -Filter "Name = 'fusion_canvas.exe'" `
            -ErrorAction SilentlyContinue | Where-Object {
                Paths-Equal $_.ExecutablePath $executablePath
            }
        if ($null -ne $sameExecutable) {
            Write-LauncherLog "refused_unowned_duplicate"
            exit 3
        }

        $preflight = Start-Process `
            -FilePath $executablePath `
            -ArgumentList @(
                "--verify-capture-isolation",
                "--log-file", ('"{0}"' -f $preflightLogPath)) `
            -WorkingDirectory $projectRoot `
            -WindowStyle Hidden `
            -RedirectStandardOutput $preflightStdoutPath `
            -RedirectStandardError $preflightStderrPath `
            -Wait `
            -PassThru
        if ($preflight.ExitCode -ne 0) {
            throw "Fusion capture-isolation preflight failed with code $($preflight.ExitCode)"
        }
    }

    $nativeState = Get-OwnedNativeState
    if ($null -eq $nativeState) {
        if (-not (Test-Path -LiteralPath $nativeStartScript -PathType Leaf)) {
            throw "Native background start script not found: $nativeStartScript"
        }
        $env:FUSION_SESSION = $session
        $nativeStartProcess = Start-Process `
            -FilePath $powerShellPath `
            -ArgumentList @(
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-File", ('"{0}"' -f $nativeStartScript)) `
            -WorkingDirectory $projectRoot `
            -WindowStyle Hidden `
            -PassThru
        if (-not $nativeStartProcess.WaitForExit(30000)) {
            Stop-Process -Id $nativeStartProcess.Id -Force -ErrorAction SilentlyContinue
            throw "Native background launcher did not exit within 30 seconds"
        }
        if ($nativeStartProcess.ExitCode -ne 0) {
            throw "Native background runtime start failed with code $($nativeStartProcess.ExitCode)"
        }
        $nativeStartedByFusion = $true

        $nativeReadyDeadline = [DateTime]::UtcNow.AddSeconds(15)
        while ($null -eq $nativeState -and
               [DateTime]::UtcNow -lt $nativeReadyDeadline) {
            Start-Sleep -Milliseconds 100
            $nativeState = Get-OwnedNativeState
        }
        if ($null -eq $nativeState) {
            throw "Native background runtime did not publish an owned state within 15 seconds"
        }
    }

    if ($nativeState.fusion_channel_enabled -ne $true) {
        throw "Native runtime was not started in Fusion standby; stop and restart the native background runtime once"
    }
    $nativeProcessId = [int]$nativeState.process_id
    $nativeSession = [string]$nativeState.fusion_session
    if ([string]::IsNullOrWhiteSpace($nativeSession)) {
        throw "Native runtime state does not contain a Fusion session"
    }
    $session = $nativeSession

    if ($ownedCanvasRunning) {
        if (-not [string]::Equals(
                $canvasSession,
                $nativeSession,
                [System.StringComparison]::Ordinal)) {
            throw "Canvas/native Fusion session mismatch: canvas=$canvasSession native=$nativeSession"
        }
        Write-LauncherLog "already_running pid=$ownedCanvasProcessId native_pid=$nativeProcessId session=$session"
        exit 0
    }

    $canvasProcess = Start-Process `
        -FilePath $executablePath `
        -ArgumentList @(
            "--session", $session,
            "--max-fps", $maxFps,
            "--idle-mode", $idleMode,
            "--log-file", ('"{0}"' -f $canvasLogPath)) `
        -WorkingDirectory $projectRoot `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru

    $ready = $false
    $readyDeadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $readyDeadline) {
        Start-Sleep -Milliseconds 100
        $canvasProcess.Refresh()
        if ($canvasProcess.HasExited) {
            throw "Fusion canvas exited during startup with code $($canvasProcess.ExitCode)"
        }
        if (Test-Path -LiteralPath $canvasLogPath -PathType Leaf) {
            try {
                $canvasLog = Get-Content -LiteralPath $canvasLogPath -Raw -ErrorAction Stop
            } catch [System.IO.IOException] {
                # Antivirus and filesystem filters may briefly hold the file
                # between creation and the first readiness record.
                continue
            }
            if (-not [string]::IsNullOrEmpty($canvasLog) -and
                $canvasLog.Contains("[FusionCanvas] channel connected") -and
                $canvasLog.Contains("[FusionCanvas] running")) {
                $ready = $true
                break
            }
        }
    }
    if (-not $ready) {
        throw "Fusion canvas did not connect to the native channel within 15 seconds"
    }

    $state = [ordered]@{
        process_id = $canvasProcess.Id
        executable_path = $executablePath
        native_process_id = $nativeProcessId
        native_started_by_fusion = $nativeStartedByFusion
        session = $session
        max_fps = $maxFps
        idle_mode = $idleMode
        canvas_log_path = $canvasLogPath
        started_at_utc = [DateTime]::UtcNow.ToString("o")
    }
    $temporaryStatePath = "$statePath.tmp.$PID"
    $state | ConvertTo-Json | Set-Content -LiteralPath $temporaryStatePath -Encoding UTF8
    Move-Item -LiteralPath $temporaryStatePath -Destination $statePath -Force
    Write-LauncherLog "started pid=$($canvasProcess.Id) native_pid=$nativeProcessId native_started_by_fusion=$nativeStartedByFusion session=$session"
    exit 0
} catch {
    if ($null -ne $canvasProcess) {
        try {
            $canvasProcess.Refresh()
            if (-not $canvasProcess.HasExited) {
                Stop-Process -Id $canvasProcess.Id -Force -ErrorAction SilentlyContinue
            }
        } catch {
        }
    }
    Write-LauncherLog "failed error=$($_.Exception.Message)"
    exit 1
}
