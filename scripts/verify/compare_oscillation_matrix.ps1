param([Parameter(Mandatory = $true)][string]$Jobs)
$ErrorActionPreference = 'Stop'
$pairs = @(Get-Content -LiteralPath $Jobs -Raw | ConvertFrom-Json)
$comparator = Join-Path $PSScriptRoot 'compare_sustained_aimlab.ps1'
$results = foreach ($pair in $pairs) {
    $lines = [System.Collections.Generic.List[string]]::new()
    $code = 0
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        # Reuse the accepted comparator, including all hard gates, in one host.
        # Streaming retains diagnostics emitted before a terminating failure.
        & $comparator -Baseline $pair.baseline -Candidate $pair.candidate |
            Out-String -Stream -Width 4096 |
            ForEach-Object { $lines.Add($_) }
    } catch {
        $code = 1
        $lines.Add($_.Exception.Message)
    }
    $timer.Stop()
    [pscustomobject]@{
        id = $pair.id
        exit_code = $code
        seconds = $timer.Elapsed.TotalSeconds
        log = $lines -join "`n"
    }
}
ConvertTo-Json -InputObject @($results) -Depth 8 -Compress
