param(
    [Parameter(Mandatory = $true)][string]$InputDir,
    [string]$JsonOutput = ""
)

$ErrorActionPreference = "Stop"
$InputDir = (Resolve-Path $InputDir).Path
$rows = foreach ($file in Get-ChildItem $InputDir -Filter *.json | Sort-Object Name) {
    $document = Get-Content $file.FullName -Raw | ConvertFrom-Json
    $runs = @($document.runs)
    $handoffs = ($runs | Measure-Object handoff_episodes -Sum).Sum
    $defects = ($runs | Measure-Object handoff_defect_episodes -Sum).Sum
    $local = ($runs | Measure-Object post_handoff_local_error_area_px_ms -Sum).Sum
    $tail = ($runs | Measure-Object post_handoff_tail_error_area_px_ms -Sum).Sum
    $tracking = ($runs | Measure-Object tracking_points -Sum).Sum
    $circleExits = ($runs | Measure-Object circle_exit_events -Sum).Sum
    $stallRing = ($runs | Measure-Object stall_ring_ms -Sum).Sum
    $targetEpisodes = @(
        foreach ($run in $runs) {
            @($run.targets) | Where-Object handoff_observed
        }
    )
    $rebounds = @($targetEpisodes | ForEach-Object post_handoff_rebound_px | Sort-Object)
    $wrongWay = @(
        $targetEpisodes |
            ForEach-Object post_handoff_wrong_way_output_integral |
            Sort-Object
    )
    function Percentile([object[]]$Values, [double]$Fraction) {
        if ($Values.Count -eq 0) { return 0.0 }
        $position = $Fraction * ($Values.Count - 1)
        $lower = [math]::Floor($position)
        $upper = [math]::Ceiling($position)
        $blend = $position - $lower
        return [double]$Values[$lower] +
            ([double]$Values[$upper] - [double]$Values[$lower]) * $blend
    }
    [pscustomobject]@{
        configuration = $file.BaseName
        runs = $runs.Count
        handoffs = $handoffs
        defects = $defects
        defect_rate_pct = if ($handoffs) { 100.0 * $defects / $handoffs } else { 0.0 }
        local_area_per_handoff = if ($handoffs) { $local / $handoffs } else { 0.0 }
        tail_area_per_handoff = if ($handoffs) { $tail / $handoffs } else { 0.0 }
        rebound_p50_px = Percentile $rebounds 0.50
        rebound_p95_px = Percentile $rebounds 0.95
        rebound_worst_px = if ($rebounds.Count) { [double]$rebounds[-1] } else { 0.0 }
        wrong_way_p95 = Percentile $wrongWay 0.95
        tracking_points = $tracking
        circle_exits = $circleExits
        stall_ring_ms = $stallRing
    }
}

$rows | Format-Table configuration, runs, handoffs, defects, defect_rate_pct,
    local_area_per_handoff, tail_area_per_handoff, rebound_p50_px,
    rebound_p95_px, rebound_worst_px, wrong_way_p95, tracking_points -AutoSize
if ($JsonOutput) {
    $rows | ConvertTo-Json -Depth 4 |
        Set-Content -Path $JsonOutput -Encoding utf8
}
