# Compares matched sustained-aimlab production reports and writes pulse/strength deltas.
# Run: powershell -File scripts/verify/compare_sustained_pulse_smoothing.ps1 -BaselineDir <dir> -CandidateDir <dir> -OutputDir <dir>
# Needs: matching production-v1 JSON reports generated with identical simulator settings and run keys.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BaselineDir,
    [Parameter(Mandatory = $true)][string]$CandidateDir,
    [Parameter(Mandatory = $true)][string]$OutputDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$metricSpecs = @(
    [pscustomobject]@{ name = 'mean_error_px'; label = 'Mean error (px)'; aggregate = 'average' },
    [pscustomobject]@{ name = 'p95_error_px'; label = 'P95 error (px)'; aggregate = 'average' },
    [pscustomobject]@{ name = 'tracking_points'; label = 'Tracking points'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'targets_acquired'; label = 'Targets acquired'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'settled_targets'; label = 'Settled targets'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'false_stop_events'; label = 'False-stop events'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'oscillation_active_ms'; label = 'Oscillation active (ms)'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'oscillation_episodes'; label = 'Oscillation episodes'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'near_center_shaped_wrong_way_ms'; label = 'Near-center wrong-way (ms)'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'correction_reversal_events'; label = 'Correction reversals'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'direction_discontinuities'; label = 'Direction discontinuities'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'controller_residual_kick_events'; label = 'Controller residual kicks'; aggregate = 'sum' },
    [pscustomobject]@{ name = 'p95_output_delta'; label = 'P95 output delta'; aggregate = 'average' },
    [pscustomobject]@{ name = 'p95_jerk'; label = 'P95 jerk'; aggregate = 'average' },
    [pscustomobject]@{ name = 'p95_first_assist_output_ms'; label = 'P95 first assist (ms)'; aggregate = 'average' }
)

function Get-RunKey($run) {
    return @(
        $run.seed,
        $run.profile,
        $run.cohort,
        $run.left_strafe,
        $run.vertical_motion,
        $run.script_hash,
        $run.ticks
    ) -join '|'
}

function Get-Aggregate($runs, [string]$metric, [string]$aggregate) {
    $measurement = $runs | Measure-Object -Property $metric -Average -Sum
    if ($aggregate -eq 'sum') {
        return [double]$measurement.Sum
    }
    return [double]$measurement.Average
}

function Get-MetricComparison($baselineRuns, $candidateRuns, $spec) {
    $baseline = Get-Aggregate $baselineRuns $spec.name $spec.aggregate
    $candidate = Get-Aggregate $candidateRuns $spec.name $spec.aggregate
    $delta = $candidate - $baseline
    $deltaPct = if ([math]::Abs($baseline) -gt 1e-12) {
        100.0 * $delta / $baseline
    } else {
        $null
    }
    return [pscustomobject]@{
        name = $spec.name
        label = $spec.label
        aggregate = $spec.aggregate
        baseline = $baseline
        candidate = $candidate
        delta = $delta
        delta_pct = $deltaPct
    }
}

function Get-Comparison($baselineRuns, $candidateRuns) {
    $metrics = foreach ($spec in $metricSpecs) {
        Get-MetricComparison $baselineRuns $candidateRuns $spec
    }
    return @($metrics)
}

$baselinePath = (Resolve-Path -LiteralPath $BaselineDir).Path
$candidatePath = (Resolve-Path -LiteralPath $CandidateDir).Path
$baselineFiles = @(Get-ChildItem -LiteralPath $baselinePath -Filter '*.json' -File |
    Where-Object Name -ne 'COMPARISON.json' |
    Sort-Object Name)
if ($baselineFiles.Count -eq 0) {
    throw "No baseline JSON reports found in $baselinePath"
}

$allBaselineRuns = [System.Collections.Generic.List[object]]::new()
$allCandidateRuns = [System.Collections.Generic.List[object]]::new()
$fileComparisons = [System.Collections.Generic.List[object]]::new()
$identity = [System.Collections.Generic.List[object]]::new()

foreach ($baselineFile in $baselineFiles) {
    $candidateFile = Join-Path $candidatePath $baselineFile.Name
    if (-not (Test-Path -LiteralPath $candidateFile -PathType Leaf)) {
        throw "Missing candidate report: $candidateFile"
    }
    $baseline = Get-Content -Raw -LiteralPath $baselineFile.FullName | ConvertFrom-Json
    $candidate = Get-Content -Raw -LiteralPath $candidateFile | ConvertFrom-Json
    if ($baseline.schema -ne 'sustained-aimlab-production-v1' -or $candidate.schema -ne $baseline.schema) {
        throw "Schema mismatch in $($baselineFile.Name)"
    }
    foreach ($field in @('config_path', 'control_path')) {
        if ($baseline.$field -ne $candidate.$field) {
            throw "Identity mismatch for $field in $($baselineFile.Name)"
        }
    }
    if (($baseline.simulator | ConvertTo-Json -Compress) -ne
        ($candidate.simulator | ConvertTo-Json -Compress)) {
        throw "Simulator mismatch in $($baselineFile.Name)"
    }
    if ($baseline.runs.Count -ne $candidate.runs.Count) {
        throw "Run-count mismatch in $($baselineFile.Name)"
    }

    $candidateByKey = @{}
    foreach ($run in $candidate.runs) {
        $key = Get-RunKey $run
        if ($candidateByKey.ContainsKey($key)) {
            throw "Duplicate candidate run key in $($baselineFile.Name): $key"
        }
        $candidateByKey[$key] = $run
    }
    $matchedCandidateRuns = [System.Collections.Generic.List[object]]::new()
    foreach ($run in $baseline.runs) {
        $key = Get-RunKey $run
        if (-not $candidateByKey.ContainsKey($key)) {
            throw "Missing candidate run key in $($baselineFile.Name): $key"
        }
        $allBaselineRuns.Add($run)
        $allCandidateRuns.Add($candidateByKey[$key])
        $matchedCandidateRuns.Add($candidateByKey[$key])
    }

    $identity.Add([pscustomobject]@{
        report = $baselineFile.Name
        baseline_revision = $baseline.revision
        candidate_revision = $candidate.revision
        baseline_dirty = [bool]$baseline.dirty
        candidate_dirty = [bool]$candidate.dirty
        run_count = $baseline.runs.Count
        simulator = $baseline.simulator
    })
    $fileComparisons.Add([pscustomobject]@{
        report = $baselineFile.Name
        run_count = $baseline.runs.Count
        metrics = Get-Comparison @($baseline.runs) @($matchedCandidateRuns)
    })
}

$segmentComparisons = [System.Collections.Generic.List[object]]::new()
foreach ($cohort in @('ads', 'bodylock')) {
    $baselineSegment = @($allBaselineRuns | Where-Object cohort -eq $cohort)
    $candidateSegment = @($allCandidateRuns | Where-Object cohort -eq $cohort)
    $segmentComparisons.Add([pscustomobject]@{
        segment = "cohort=$cohort"
        run_count = $baselineSegment.Count
        metrics = Get-Comparison $baselineSegment $candidateSegment
    })
}
foreach ($vertical in @('off', 'slide', 'jump', 'random')) {
    $baselineSegment = @($allBaselineRuns | Where-Object vertical_motion -eq $vertical)
    $candidateSegment = @($allCandidateRuns | Where-Object vertical_motion -eq $vertical)
    $segmentComparisons.Add([pscustomobject]@{
        segment = "vertical=$vertical"
        run_count = $baselineSegment.Count
        metrics = Get-Comparison $baselineSegment $candidateSegment
    })
}

$result = [pscustomobject]@{
    schema = 'sustained-pulse-smoothing-comparison-v1'
    generated_at = (Get-Date).ToUniversalTime().ToString('o')
    baseline_dir = $baselinePath
    candidate_dir = $candidatePath
    matched_reports = $baselineFiles.Count
    matched_runs = $allBaselineRuns.Count
    identity = @($identity)
    overall = [pscustomobject]@{
        run_count = $allBaselineRuns.Count
        metrics = Get-Comparison @($allBaselineRuns) @($allCandidateRuns)
    }
    by_report = @($fileComparisons)
    by_segment = @($segmentComparisons)
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$jsonPath = Join-Path $OutputDir 'COMPARISON.json'
$markdownPath = Join-Path $OutputDir 'COMPARISON.md'
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$markdown = [System.Collections.Generic.List[string]]::new()
$markdown.Add('# Sustained aimlab pulse-smoothing comparison')
$markdown.Add('')
$markdown.Add("Matched reports: $($result.matched_reports); matched runs: $($result.matched_runs).")
$markdown.Add('')
$markdown.Add('All report schemas, simulator settings, config/control paths, run counts, and run keys matched.')
$markdown.Add('')
$markdown.Add('## Overall')
$markdown.Add('')
$markdown.Add('| Metric | Baseline | Candidate | Delta | Delta % |')
$markdown.Add('|---|---:|---:|---:|---:|')
foreach ($metric in $result.overall.metrics) {
    $pct = if ($null -eq $metric.delta_pct) { 'n/a' } else { '{0:N2}%' -f $metric.delta_pct }
    $markdown.Add(('| {0} | {1:N3} | {2:N3} | {3:N3} | {4} |' -f
        $metric.label, $metric.baseline, $metric.candidate, $metric.delta, $pct))
}

$markdown.Add('')
$markdown.Add('## By report')
foreach ($comparison in $result.by_report) {
    $markdown.Add('')
    $markdown.Add("### $($comparison.report) ($($comparison.run_count) runs)")
    $markdown.Add('')
    $markdown.Add('| Metric | Baseline | Candidate | Delta % |')
    $markdown.Add('|---|---:|---:|---:|')
    foreach ($metric in $comparison.metrics) {
        $pct = if ($null -eq $metric.delta_pct) { 'n/a' } else { '{0:N2}%' -f $metric.delta_pct }
        $markdown.Add(('| {0} | {1:N3} | {2:N3} | {3} |' -f
            $metric.label, $metric.baseline, $metric.candidate, $pct))
    }
}
$markdown | Set-Content -LiteralPath $markdownPath -Encoding UTF8

Write-Host "PASS: matched $($result.matched_reports) reports and $($result.matched_runs) runs"
Write-Host $jsonPath
Write-Host $markdownPath
