param(
    [Parameter(Mandatory = $true)] [string] $PreAxisPath,
    [Parameter(Mandatory = $true)] [string] $PreCoastRisePath,
    [Parameter(Mandatory = $true)] [string] $CurrentPath,
    [Parameter(Mandatory = $true)] [string] $MarkdownOutput,
    [Parameter(Mandatory = $true)] [string] $JsonOutput
)

$ErrorActionPreference = 'Stop'

$metricNames = @(
    'mean_error_px',
    'p95_error_px',
    'peak_error_px',
    'error_window_mean_px',
    'error_window_p95_px',
    'error_window_recovery_ms',
    'p95_output_delta',
    'output_spikes',
    'axis_intervention_x_frames',
    'axis_intervention_y_frames',
    'max_overshoot_x_px',
    'max_overshoot_y_px'
)

function Read-Artifact([string] $Path) {
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Get-NumericMetric($Metrics, [string] $Name) {
    $property = $Metrics.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return $null
    }
    return [double] $property.Value
}

function New-MetricRow(
    [string] $Scenario,
    [string] $Case,
    [string] $Metric,
    $BeforeMetrics,
    $AfterMetrics) {
    $before = Get-NumericMetric $BeforeMetrics $Metric
    $after = Get-NumericMetric $AfterMetrics $Metric
    if ($null -eq $before -or $null -eq $after) {
        return $null
    }
    $percent = if ([math]::Abs($before) -le 1e-12) {
        $null
    } else {
        100.0 * ($after - $before) / [math]::Abs($before)
    }
    return [pscustomobject] @{
        scenario = $Scenario
        case = $Case
        metric = $Metric
        before = $before
        after = $after
        delta = $after - $before
        percent_change = $percent
    }
}

function Compare-Artifacts(
    [string] $FromRevision,
    [string] $ToRevision,
    $Before,
    $After) {
    $rows = @()
    foreach ($beforeScenario in @($Before.scenarios)) {
        $afterScenario = @($After.scenarios) |
            Where-Object { $_.name -eq $beforeScenario.name } |
            Select-Object -First 1
        if ($null -eq $afterScenario) { continue }
        foreach ($metric in $metricNames) {
            $row = New-MetricRow `
                $beforeScenario.name '' $metric `
                $beforeScenario.metrics $afterScenario.metrics
            if ($null -ne $row) { $rows += $row }
        }
        foreach ($beforeCase in @($beforeScenario.cases)) {
            $afterCase = @($afterScenario.cases) |
                Where-Object { $_.name -eq $beforeCase.name } |
                Select-Object -First 1
            if ($null -eq $afterCase) { continue }
            foreach ($metric in $metricNames) {
                $row = New-MetricRow `
                    $beforeScenario.name $beforeCase.name $metric `
                    $beforeCase.metrics $afterCase.metrics
                if ($null -ne $row) { $rows += $row }
            }
        }
    }
    return [pscustomobject] @{
        from_revision = $FromRevision
        to_revision = $ToRevision
        label = "$FromRevision -> $ToRevision"
        rows = $rows
    }
}

function Format-Number($Value) {
    if ($null -eq $Value) { return 'n/a' }
    return ([double] $Value).ToString('0.######', [Globalization.CultureInfo]::InvariantCulture)
}

$preAxis = Read-Artifact $PreAxisPath
$preCoast = Read-Artifact $PreCoastRisePath
$current = Read-Artifact $CurrentPath
$comparisons = @(
    Compare-Artifacts '0710d11' 'a92ad73' $preCoast $current
    Compare-Artifacts '990946c' 'a92ad73' $preAxis $current
)

$summary = [pscustomobject] @{
    seed = $current.seed
    inputs = [pscustomobject] @{
        pre_axis = $PreAxisPath
        pre_coast_rise = $PreCoastRisePath
        current = $CurrentPath
    }
    comparisons = $comparisons
}

$markdown = New-Object System.Text.StringBuilder
[void] $markdown.AppendLine('# Axis Stress A/B Results')
[void] $markdown.AppendLine('')
[void] $markdown.AppendLine("Seed: $($current.seed)")
foreach ($comparison in $comparisons) {
    [void] $markdown.AppendLine('')
    [void] $markdown.AppendLine("## $($comparison.label)")
    [void] $markdown.AppendLine('')
    [void] $markdown.AppendLine('| Scenario | Case | Metric | Before | After | Delta | Change |')
    [void] $markdown.AppendLine('|---|---|---|---:|---:|---:|---:|')
    foreach ($row in $comparison.rows) {
        $caseName = if ([string]::IsNullOrEmpty($row.case)) { '(aggregate)' } else { $row.case }
        $percent = if ($null -eq $row.percent_change) {
            'n/a'
        } else {
            (Format-Number $row.percent_change) + '%'
        }
        [void] $markdown.AppendLine(
            "| $($row.scenario) | $caseName | $($row.metric) | " +
            "$(Format-Number $row.before) | $(Format-Number $row.after) | " +
            "$(Format-Number $row.delta) | $percent |")
    }
}

foreach ($path in @($MarkdownOutput, $JsonOutput)) {
    $parent = Split-Path -Parent $path
    if (-not [string]::IsNullOrEmpty($parent)) {
        [void] [IO.Directory]::CreateDirectory($parent)
    }
}
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($MarkdownOutput, $markdown.ToString(), $utf8NoBom)
[IO.File]::WriteAllText(
    $JsonOutput,
    ($summary | ConvertTo-Json -Depth 12),
    $utf8NoBom)
