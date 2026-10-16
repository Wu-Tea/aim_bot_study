param([ValidateRange(1,24)][int]$Hours=12)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path
New-Item -ItemType Directory -Path "$root/runs/desktop" -Force|Out-Null
if(Test-Path "$root/runs/desktop/long-capture.json"){
    $previous=Get-Content "$root/runs/desktop/long-capture.json" -Raw|ConvertFrom-Json
    if($previous.enabled -and [DateTime]::Parse($previous.expires_utc).ToUniversalTime() -gt [DateTime]::UtcNow){
        throw 'Capture already armed; stop the previous system capture before arming another.'
    }
}
if([IO.DriveInfo]::new([IO.Path]::GetPathRoot($root)).AvailableFreeSpace -lt 24GB){throw 'Need at least 24 GiB free for a long capture.'}
$directory=Join-Path $root ('output/long-play-'+(Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $directory|Out-Null
$hostExe=(Get-Process -Id $PID).Path
$monitor=Start-Process -FilePath $hostExe -ArgumentList @('-NoProfile','-File',('"'+$PSScriptRoot+'/monitor.ps1"'),'-Directory',('"'+$directory+'"'),'-Hours',$Hours) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$directory/monitor.stdout.log" -RedirectStandardError "$directory/monitor.stderr.log"
$until=[DateTime]::UtcNow.AddSeconds(30)
do {Start-Sleep -Milliseconds 300;if($monitor.HasExited){throw "Monitor exited; inspect $directory"}} while(!(Test-Path "$directory/monitor-health.json") -and [DateTime]::UtcNow -lt $until)
if(!(Test-Path "$directory/monitor-health.json")){throw "Monitor startup timed out: $directory"}
$health=Get-Content "$directory/monitor-health.json" -Raw|ConvertFrom-Json
if($health.failed -or !$health.gpu_running -or !$health.presentmon_running){throw "Collectors unavailable: $directory"}
$marker=@{schema='long-capture-v2';enabled=$true;directory=$directory;expires_utc=[DateTime]::UtcNow.AddHours($Hours).ToString('o')}
$marker|ConvertTo-Json|Set-Content -Encoding utf8 "$root/runs/desktop/long-capture.json"
Copy-Item -LiteralPath "$PSScriptRoot/README.txt" -Destination "$directory/README.txt"
Write-Output "Capture armed for $Hours hours. Use the normal desktop GUI to start/stop the assistant. Directory: $directory"
