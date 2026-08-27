param(
    [Parameter(Mandatory = $true)][string]$Baseline,
    [Parameter(Mandatory = $true)][string]$Candidate,
    [string]$Policy = "",
    [switch]$AllowIntentFusionDifference,
    [switch]$ReportOnly
)

$ErrorActionPreference = "Stop"
$baselinePath = (Resolve-Path $Baseline).Path
$candidatePath = (Resolve-Path $Candidate).Path
if (-not $Policy) {
    $Policy = Join-Path $PSScriptRoot `
        "..\..\docs\benchmarks\sustained-aimlab-optimization-policy-v1.json"
}
$policyPath = (Resolve-Path $Policy).Path
$before = Get-Content $baselinePath -Raw | ConvertFrom-Json
$after = Get-Content $candidatePath -Raw | ConvertFrom-Json
$optimizationPolicy = Get-Content $policyPath -Raw | ConvertFrom-Json

if ($optimizationPolicy.schema -ne "sustained-aimlab-optimization-policy-v1" -or
    $optimizationPolicy.status -ne "accepted") {
    throw "AimLab optimization policy is not the accepted v1 schema: $policyPath"
}

function Canonical-Json($value) {
    return $value | ConvertTo-Json -Depth 20 -Compress
}

function Require-Same($label, $left, $right) {
    if ((Canonical-Json $left) -ne (Canonical-Json $right)) {
        throw "INVALID / NON-COMPARABLE: ${label} differs"
    }
}

if ($before.schema -ne $after.schema) {
    throw "INVALID / NON-COMPARABLE: report schema differs"
}
if ($before.control_path -ne $after.control_path) {
    throw "INVALID / NON-COMPARABLE: control path differs"
}
Require-Same "simulator covariates" $before.simulator $after.simulator

$beforeRuntime = $before.PSObject.Properties['runtime_profile']
$afterRuntime = $after.PSObject.Properties['runtime_profile']
if (($null -eq $beforeRuntime) -ne ($null -eq $afterRuntime)) {
    throw "INVALID / NON-COMPARABLE: runtime profile exists in only one artifact"
}
if ($null -ne $beforeRuntime) {
    foreach ($field in @(
        'id', 'profile_payload_sha256', 'audit_artifact_sha256',
        'source_runtime_sha256', 'source_config_sha256',
        'source_engine_sha256', 'config_relationship', 'observation_samples',
        'manual_segments', 'manual_samples', 'target_samples',
        'not_exact_replay')) {
        if ($before.runtime_profile.$field -ne $after.runtime_profile.$field) {
            throw "INVALID / NON-COMPARABLE: runtime profile ${field} differs"
        }
    }
}

$beforeFusion = $before.PSObject.Properties['intent_fusion']
$afterFusion = $after.PSObject.Properties['intent_fusion']
if (($null -eq $beforeFusion) -ne ($null -eq $afterFusion)) {
    throw "INVALID / NON-COMPARABLE: intent_fusion exists in only one artifact"
}
if ($null -ne $beforeFusion) {
    foreach ($field in @('schema_version', 'candidate_set_version')) {
        if ($before.intent_fusion.$field -ne $after.intent_fusion.$field) {
            throw "INVALID / NON-COMPARABLE: intent fusion ${field} differs"
        }
    }
    if (-not $AllowIntentFusionDifference -and
        $before.intent_fusion.mode -ne $after.intent_fusion.mode) {
        throw "INVALID / NON-COMPARABLE: intent fusion mode differs; pass -AllowIntentFusionDifference only for an explicit experiment"
    }
}

$beforeCf = $before.PSObject.Properties['counterfactual_conflict']
$afterCf = $after.PSObject.Properties['counterfactual_conflict']
if (($null -eq $beforeCf) -ne ($null -eq $afterCf)) {
    throw "Counterfactual metadata exists in only one artifact"
}
$compareCounterfactual = $null -ne $beforeCf
if ($compareCounterfactual) {
    foreach ($field in @('schema_version', 'candidate_set_version', 'mode',
                          'per_kind_replay_budget', 'stable_horizon_ms')) {
        if ($before.counterfactual_conflict.$field -ne
            $after.counterfactual_conflict.$field) {
            throw "Counterfactual metadata mismatch for ${field}"
        }
    }
}

function Run-Key($document, $run) {
    return "$($run.seed)|$($run.profile)|$($run.cohort)|" +
        "$($run.left_strafe)|$($run.vertical_motion)|$($run.script_hash)"
}

function Numeric-Field($object, [string]$field, [string]$key) {
    $property = $object.PSObject.Properties[$field]
    if ($null -eq $property -or $null -eq $property.Value) {
        throw "INVALID / NON-COMPARABLE: missing protected metric ${field} in ${key}"
    }
    try {
        $value = [double]$property.Value
    } catch {
        throw "INVALID / NON-COMPARABLE: protected metric ${field} is not numeric in ${key}"
    }
    if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) {
        throw "FAILED CONSTRAINTS: protected metric ${field} is not finite in ${key}"
    }
    return $value
}

$beforeByKey = @{}
foreach ($run in $before.runs) {
    $key = Run-Key $before $run
    if ($beforeByKey.ContainsKey($key)) {
        throw "INVALID / NON-COMPARABLE: duplicate baseline run: $key"
    }
    $beforeByKey[$key] = $run
}
if ($beforeByKey.Count -ne $after.runs.Count) {
    throw "INVALID / NON-COMPARABLE: run count differs"
}

$constraintViolations = [System.Collections.Generic.List[object]]::new()
$tolerance = [double]$optimizationPolicy.floating_tolerance
$rows = foreach ($run in $after.runs) {
    $key = Run-Key $after $run
    if (-not $beforeByKey.ContainsKey($key)) {
        throw "INVALID / NON-COMPARABLE: missing baseline run: $key"
    }
    $old = $beforeByKey[$key]
    if ($compareCounterfactual -and
        ($null -eq $old.PSObject.Properties['counterfactual'] -or
         $null -eq $run.PSObject.Properties['counterfactual'])) {
        throw "Missing per-run counterfactual data: $key"
    }
    foreach ($metric in $optimizationPolicy.protected_metrics) {
        $field = [string]$metric.field
        $oldValue = Numeric-Field $old $field $key
        $newValue = Numeric-Field $run $field $key
        $failed = if ($metric.direction -eq 'higher_or_equal') {
            $newValue + $tolerance -lt $oldValue
        } elseif ($metric.direction -eq 'lower_or_equal') {
            $newValue - $tolerance -gt $oldValue
        } else {
            throw "Unknown protected metric direction: $($metric.direction)"
        }
        if ($failed) {
            $constraintViolations.Add([pscustomobject]@{
                key = $key
                metric = $field
                direction = $metric.direction
                baseline = $oldValue
                candidate = $newValue
                delta = $newValue - $oldValue
            })
        }
    }
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
        causal_gap_delta_px_ms = if ($compareCounterfactual) {
            $run.counterfactual.causal_error_area_gap_px_ms -
                $old.counterfactual.causal_error_area_gap_px_ms
        } else { $null }
        future_burden_delta_px_ms = if ($compareCounterfactual) {
            $run.counterfactual.future_burden_px_ms -
                $old.counterfactual.future_burden_px_ms
        } else { $null }
        future_settle_delay_delta_ms = if ($compareCounterfactual) {
            $run.counterfactual.future_settle_delay_ms -
                $old.counterfactual.future_settle_delay_ms
        } else { $null }
        analyzed_episode_delta = if ($compareCounterfactual) {
            $run.counterfactual.analyzed_episodes -
                $old.counterfactual.analyzed_episodes
        } else { $null }
        skipped_episode_delta = if ($compareCounterfactual) {
            $run.counterfactual.skipped_episodes -
                $old.counterfactual.skipped_episodes
        } else { $null }
    }
}

$rows | Format-Table -AutoSize
if ($compareCounterfactual) {
    $rows |
        Select-Object key, causal_gap_delta_px_ms,
            future_burden_delta_px_ms, future_settle_delay_delta_ms,
            analyzed_episode_delta, skipped_episode_delta |
        Format-Table -AutoSize
}

$ranking = foreach ($metric in $optimizationPolicy.ranking_metrics) {
    $baselineTotal = 0.0
    $candidateTotal = 0.0
    foreach ($run in $after.runs) {
        $key = Run-Key $after $run
        $baselineTotal += Numeric-Field $beforeByKey[$key] $metric $key
        $candidateTotal += Numeric-Field $run $metric $key
    }
    [pscustomobject]@{
        metric = $metric
        baseline = $baselineTotal
        candidate = $candidateTotal
        delta = $candidateTotal - $baselineTotal
    }
}

if ($constraintViolations.Count -gt 0) {
    $constraintViolations |
        Sort-Object key, metric |
        ForEach-Object {
            Write-Output (
                "[PROTECTED-REGRESSION] metric=$($_.metric) " +
                "baseline=$($_.baseline) candidate=$($_.candidate) " +
                "delta=$($_.delta) run=$($_.key)")
        }
    Write-Output "AIMLAB_CONSTRAINT_GATE=FAILED CONSTRAINTS"
    Write-Output "Ranking scores are not eligible for a recommendation."
    if (-not $ReportOnly) {
        throw "$($constraintViolations.Count) protected AimLab metric regression(s)"
    }
} else {
    Write-Output "AIMLAB_CONSTRAINT_GATE=BENCHMARK-ELIGIBLE"
    $ranking | Format-Table -AutoSize
    Write-Output "This gate does not replace missing scenario coverage or matched live acceptance."
}
