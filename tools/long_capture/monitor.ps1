param([Parameter(Mandatory=$true)][string]$Directory,[int]$Hours=12)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path
$Directory=(Resolve-Path -LiteralPath $Directory).Path
$deadline=[DateTime]::UtcNow.AddHours($Hours)
$presentExe='D:/env/UE_5.6/Engine/Binaries/ThirdParty/PresentMon/Win64/PresentMon-1.8.0-x64.exe'
$session='AimLabCapture_'+$PID
$gpu=$null;$present=$null;$writer=$null;$count=0;$failed=$false;$failureReason=''
$monitorProcess=Get-Process -Id $PID
function Process-Identity($process) {
    if($null -eq $process){return $null}
    return @{pid=$process.Id;created=$process.StartTime.ToUniversalTime().ToFileTimeUtc()}
}
$monitorIdentity=Process-Identity $monitorProcess
$gpuIdentity=$null;$presentIdentity=$null
function Save-Health($complete,$reason='') {
    @{schema='long-capture-v2';utc=[DateTime]::UtcNow.ToString('o');pid=$PID;samples=$count;complete=$complete;failed=$failed;reason=$reason;
      processes=@{monitor=$monitorIdentity;gpu=$gpuIdentity;presentmon=$presentIdentity};
      gpu_running=($null -ne $gpu -and !$gpu.HasExited);presentmon_running=($null -ne $present -and !$present.HasExited)} |
        ConvertTo-Json | Set-Content -Encoding utf8 "$Directory/monitor-health.tmp"
    if(Test-Path "$Directory/monitor-health.json"){
        [IO.File]::Replace("$Directory/monitor-health.tmp","$Directory/monitor-health.json",[NullString]::Value)
    }else{[IO.File]::Move("$Directory/monitor-health.tmp","$Directory/monitor-health.json")}
}
try {
    $fields='timestamp,name,memory.total,memory.used,memory.free,utilization.gpu,utilization.memory,temperature.gpu,clocks.current.graphics,clocks.current.memory,power.draw,power.limit,pstate,clocks_event_reasons.active'
    $gpu=Start-Process -FilePath (Get-Command nvidia-smi).Source -ArgumentList "--query-gpu=$fields --format=csv,nounits -lms 1000" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$Directory/gpu.csv" -RedirectStandardError "$Directory/gpu-errors.log"
    $gpuIdentity=Process-Identity $gpu
    # Capture by PID even if Windows denies PresentMon's process-name lookup.
    # Join the game's PID from system.jsonl; never silently filter it out.
    $present=Start-Process -FilePath $presentExe -ArgumentList @('-captureall','-output_file',('"'+$Directory+'/presents.csv"'),'-qpc_time','-no_top','-session_name',$session,'-timed',($Hours*3600),'-terminate_after_timed') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$Directory/presentmon.stdout.log" -RedirectStandardError "$Directory/presentmon.stderr.log"
    $presentIdentity=Process-Identity $present
    $writer=[IO.StreamWriter]::new("$Directory/system.jsonl",$false)
    $writer.AutoFlush=$true
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CaptureForeground {
 [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint p);
 public static uint Pid(){uint p;GetWindowThreadProcessId(GetForegroundWindow(),out p);return p;}
}
'@
    @{schema='long-capture-v2';started_utc=[DateTime]::UtcNow.ToString('o');deadline_utc=$deadline.ToString('o');timezone=[TimeZoneInfo]::Local.Id;
      qpc_frequency=[Diagnostics.Stopwatch]::Frequency;presentmon=$presentExe;presentmon_pid=$present.Id;nvidia_pid=$gpu.Id;
      cpu=@(Get-CimInstance Win32_Processor|Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);
      gpu=@(& nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv)}|ConvertTo-Json -Depth 5|Set-Content -Encoding utf8 "$Directory/environment.json"
    while([DateTime]::UtcNow -lt $deadline -and !(Test-Path -LiteralPath "$Directory/stop")) {
        $before=[Diagnostics.Stopwatch]::GetTimestamp();$stamp=[DateTime]::UtcNow.ToString('o');$errors=@()
        $engines=@();$memory=@();$os=$null
        try{$engines=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine | Where-Object UtilizationPercentage -gt 0 | Select-Object Name,UtilizationPercentage)}catch{$errors+='gpu_engine: '+$_.Exception.Message}
        try{$memory=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory | Select-Object Name,DedicatedUsage,SharedUsage,TotalCommitted)}catch{$errors+='gpu_memory: '+$_.Exception.Message}
        try{$os=Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory | Select-Object AvailableMBytes,CommittedBytes,CommitLimit,PagesInputPersec}catch{$errors+='ram: '+$_.Exception.Message}
        $processes=@(Get-Process cod,cod_native_runtime -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,CPU,WorkingSet64,PrivateMemorySize64,HandleCount,StartTime)
        $free=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($Directory)).AvailableFreeSpace
        $record=@{utc=$stamp;qpc_before=$before;qpc_after=[Diagnostics.Stopwatch]::GetTimestamp();foreground_pid=[CaptureForeground]::Pid();
          gpu_engines=$engines;gpu_process_memory=$memory;processes=$processes;memory=$os;disk_free_bytes=$free;errors=$errors}
        $writer.WriteLine(($record|ConvertTo-Json -Depth 6 -Compress));++$count
        if($gpu.HasExited -or $present.HasExited){throw 'GPU or PresentMon collector exited unexpectedly; inspect collector logs.'}
        Save-Health $false
        if($free -lt 12GB){throw 'Less than 12 GiB free: system collectors stopped; native diagnostics have an independent disk reserve.'}
        Start-Sleep -Seconds 2
    }
}catch{$failed=$true;$failureReason=$_.Exception.Message;Save-Health $false $failureReason}
finally {
    if($writer){$writer.Dispose()}
    if($present -and !$present.HasExited){
        $end=Start-Process -FilePath $presentExe -ArgumentList @('-session_name',$session,'-terminate_existing') -WindowStyle Hidden -PassThru
        $null=$end.WaitForExit(5000);$null=$present.WaitForExit(5000)
    }
    if($gpu -and !$gpu.HasExited){Stop-Process -Id $gpu.Id}
    Save-Health (!$failed) $failureReason
}
