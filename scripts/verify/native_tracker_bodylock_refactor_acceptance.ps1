param(
    [Parameter(Mandatory = $true)]
    [string]$FinalArtifact,
    [Parameter(Mandatory = $true)]
    [string]$CleanBaselineArtifact
)

$ErrorActionPreference = "Stop"

function Read-Artifact([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "benchmark artifact not found: $Path"
    }
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Find-Scenario($Artifact, [string]$Name) {
    $scenario = $Artifact.scenarios | Where-Object { $_.name -eq $Name } | Select-Object -First 1
    if ($null -eq $scenario) {
        throw "scenario missing from artifact: $Name"
    }
    return $scenario
}

$final = Read-Artifact $FinalArtifact
$baseline = Read-Artifact $CleanBaselineArtifact
$failures = [System.Collections.Generic.List[string]]::new()

function Require-Gate([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        $script:failures.Add($Message)
    }
}

foreach ($name in @(
    "bodylock_continuity_defect_100hz",
    "bodylock_mode_chatter_defect_100hz"
)) {
    $metrics = (Find-Scenario $final $name).bodylock_continuity
    Require-Gate ($metrics.benchmark_status -eq "PASS") "$name benchmark status is not PASS"
    Require-Gate ($metrics.authority_loss_override_events -eq 0) "$name authority-loss overrides != 0"
    Require-Gate ($metrics.max_excess_final_delta -le 0.15) "$name max excess final delta > 0.15"
    Require-Gate ($metrics.assist_delta_p95 -le 0.10) "$name assist delta p95 > 0.10"
    Require-Gate ($metrics.final_jerk_p95 -le 0.15) "$name final jerk p95 > 0.15"
    Require-Gate ($metrics.longest_opposing_assist_ms -le 8.0) "$name opposing assist > 8 ms"
    Require-Gate ($metrics.longest_manual_gain_plateau_ms -le 8.0) "$name gain plateau > 8 ms"
    Require-Gate ($metrics.short_body_lock_runs -eq 0) "$name contains short bodylock runs"
}

$positiveScenarios = @(
    "ads_bodylock_moving_chase_100hz_dynamic",
    "ads_bodylock_slide_visible_chase_100hz_dynamic",
    "ads_bodylock_slide_occlusion_chase_100hz_dynamic_fire",
    "ads_bodylock_crouch_cycle_chase_100hz_dynamic",
    "ads_bodylock_jump_chase_100hz_dynamic",
    "ads_bodylock_arc_jump_chase_100hz_dynamic"
)

foreach ($name in $positiveScenarios) {
    $current = (Find-Scenario $final $name).ads_manual_stress
    $before = (Find-Scenario $baseline $name).ads_manual_stress
    $minimumFrames = [Math]::Ceiling([double]$before.body_lock_frames * 0.95)
    $currentDropoutRate = [double]$current.body_lock_dropout_frames / [Math]::Max(1.0, [double]$current.measured_ticks)
    $beforeDropoutRate = [double]$before.body_lock_dropout_frames / [Math]::Max(1.0, [double]$before.measured_ticks)
    $errorAllowance = [Math]::Max([double]$before.body_lock_p95_error_px * 1.05, [double]$before.body_lock_p95_error_px + 0.5)
    $overshootAllowance = [Math]::Max([double]$before.max_overshoot_px * 1.05, [double]$before.max_overshoot_px + 0.5)

    Require-Gate ($current.body_lock_frames -ge $minimumFrames) "$name bodylock frames below 95% of clean A0"
    Require-Gate ($currentDropoutRate -le ($beforeDropoutRate + 0.01)) "$name dropout rate regressed by > 1 percentage point"
    Require-Gate ($current.body_lock_sustain_pass_rate -ge ($before.body_lock_sustain_pass_rate - 0.01)) "$name sustain pass rate regressed"
    Require-Gate ($current.body_lock_p95_error_px -le $errorAllowance) "$name bodylock p95 error exceeded allowance"
    Require-Gate ($current.max_overshoot_px -le $overshootAllowance) "$name max overshoot exceeded allowance"
    Require-Gate ($current.body_lock_p95_output_delta -le 0.15) "$name bodylock output delta p95 > 0.15"
    Require-Gate ($current.unreliable_high_output_frames -le $before.unreliable_high_output_frames) "$name unreliable high-output frames increased"
    Require-Gate ($current.unreliable_fight_frames -le $before.unreliable_fight_frames) "$name unreliable user-fight frames increased"
}

$escape = Find-Scenario $final "bodylock_edge_escape"
Require-Gate ($escape.x.mean_manual_preservation_ratio -ge 0.90) "bodylock edge-escape manual preservation < 0.90"

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Error "[TrackerBodylockAcceptance] $failure"
    }
    throw "tracker/bodylock acceptance failed with $($failures.Count) gate violation(s)"
}

Write-Output "[TrackerBodylockAcceptance] PASS"
Write-Output "  final=$FinalArtifact"
Write-Output "  clean_baseline=$CleanBaselineArtifact"
Write-Output "  continuity_scenarios=2 positive_scenarios=$($positiveScenarios.Count)"
