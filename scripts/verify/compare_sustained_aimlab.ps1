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
        settled_target_delta = $run.settled_targets - $old.settled_targets
        center_cross_delta = $run.center_cross_events - $old.center_cross_events
        post_cross_max_delta_px =
            $run.max_post_cross_error_px - $old.max_post_cross_error_px
        overshoot_area_delta_px_ms =
            $run.overshoot_area_px_ms - $old.overshoot_area_px_ms
        continued_push_delta_ms =
            $run.continued_push_after_cross_ms - $old.continued_push_after_cross_ms
        circle_exit_delta = $run.circle_exit_events - $old.circle_exit_events
        stall_ring_delta_ms = $run.stall_ring_ms - $old.stall_ring_ms
        correction_reversal_delta =
            $run.correction_reversal_events - $old.correction_reversal_events
        over_delta = $run.over_events - $old.over_events
        false_interrupt_delta = $run.false_interruption_events - $old.false_interruption_events
        false_stop_delta = $run.false_stop_events - $old.false_stop_events
        bodylock_entry_failure_delta =
            $run.bodylock_entry_failures - $old.bodylock_entry_failures
    }
}

$rows | Format-Table -AutoSize
