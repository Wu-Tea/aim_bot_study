param(
    [Parameter(Mandatory = $true)][string]$Baseline,
    [Parameter(Mandatory = $true)][string]$Candidate
)

$ErrorActionPreference = "Stop"
$before = Get-Content (Resolve-Path $Baseline) -Raw | ConvertFrom-Json
$after = Get-Content (Resolve-Path $Candidate) -Raw | ConvertFrom-Json

function Run-Key($document, $run) {
    return "$($run.seed)|$($run.profile)|$($run.cohort)|" +
        "$($document.simulator.target_profile)|" +
        "$($document.simulator.camera_response_px_per_stick_second)|" +
        "$($document.simulator.slowdown_edge)|$($document.simulator.slowdown_center)"
}

$beforeByKey = @{}
foreach ($run in $before.runs) { $beforeByKey[(Run-Key $before $run)] = $run }
$rows = foreach ($run in $after.runs) {
    $key = Run-Key $after $run
    if (-not $beforeByKey.ContainsKey($key)) { throw "Missing baseline run: $key" }
    $old = $beforeByKey[$key]
    [pscustomobject]@{
        key = $key
        acquired_delta_pct = if ($old.targets_acquired) {
            100.0 * ($run.targets_acquired - $old.targets_acquired) / $old.targets_acquired
        } else { 0.0 }
        acquire_points_delta_pct = if ($old.acquire_points) {
            100.0 * ($run.acquire_points - $old.acquire_points) / $old.acquire_points
        } else { 0.0 }
        tracking_delta_pct = if ($old.tracking_points) {
            100.0 * ($run.tracking_points - $old.tracking_points) / $old.tracking_points
        } else { 0.0 }
        p95_error_delta_pct = if ($old.p95_error_px) {
            100.0 * ($run.p95_error_px - $old.p95_error_px) / $old.p95_error_px
        } else { 0.0 }
        p95_jerk_delta_pct = if ($old.p95_jerk) {
            100.0 * ($run.p95_jerk - $old.p95_jerk) / $old.p95_jerk
        } else { 0.0 }
        over_delta = $run.over_events - $old.over_events
        false_interrupt_delta = $run.false_interruption_events - $old.false_interruption_events
        false_stop_delta = $run.false_stop_events - $old.false_stop_events
        bodylock_entry_failure_delta =
            $run.bodylock_entry_failures - $old.bodylock_entry_failures
    }
}

$rows | Format-Table -AutoSize
