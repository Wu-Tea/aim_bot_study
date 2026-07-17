Describe 'compare_axis_stress_ab' {
    It 'emits both revision comparisons and per-case metrics' {
        function New-Fixture([double] $mean, [double] $p95, [double] $delta) {
            return @{
                seed = 1337
                scenarios = @(
                    @{
                        name = 'partial_occlusion_practical_stress'
                        metrics = @{
                            mean_error_px = $mean
                            p95_error_px = $p95
                            peak_error_px = 80.0
                            error_window_mean_px = $mean + 5.0
                            error_window_p95_px = $p95 + 5.0
                            error_window_recovery_ms = 90.0
                            p95_output_delta = $delta
                            output_spikes = 2
                            axis_intervention_x_frames = 10
                            axis_intervention_y_frames = 8
                            max_overshoot_x_px = 4.0
                            max_overshoot_y_px = 3.0
                        }
                        cases = @(
                            @{
                                name = 'wrong_x'
                                metrics = @{
                                    mean_error_px = $mean
                                    p95_error_px = $p95
                                    peak_error_px = 80.0
                                    error_window_mean_px = $mean + 5.0
                                    error_window_p95_px = $p95 + 5.0
                                    error_window_recovery_ms = 90.0
                                    p95_output_delta = $delta
                                    output_spikes = 2
                                    axis_intervention_x_frames = 10
                                    axis_intervention_y_frames = 0
                                    max_overshoot_x_px = 4.0
                                    max_overshoot_y_px = 0.0
                                }
                            }
                        )
                    }
                )
            }
        }

        $preAxis = Join-Path $TestDrive '990946c.json'
        $preCoast = Join-Path $TestDrive '0710d11.json'
        $current = Join-Path $TestDrive 'a92ad73.json'
        $markdownOutput = Join-Path $TestDrive 'summary.md'
        $jsonOutput = Join-Path $TestDrive 'summary.json'
        New-Fixture 50.0 90.0 0.08 |
            ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $preAxis -Encoding UTF8
        New-Fixture 45.0 85.0 0.07 |
            ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $preCoast -Encoding UTF8
        New-Fixture 40.0 80.0 0.06 |
            ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $current -Encoding UTF8

        $scriptPath = Join-Path $PSScriptRoot '..\scripts\compare_axis_stress_ab.ps1'
        & $scriptPath `
            -PreAxisPath $preAxis `
            -PreCoastRisePath $preCoast `
            -CurrentPath $current `
            -MarkdownOutput $markdownOutput `
            -JsonOutput $jsonOutput

        Test-Path -LiteralPath $markdownOutput | Should Be $true
        Test-Path -LiteralPath $jsonOutput | Should Be $true
        $markdown = Get-Content -LiteralPath $markdownOutput -Raw
        $summary = Get-Content -LiteralPath $jsonOutput -Raw | ConvertFrom-Json
        $markdown | Should Match '0710d11 -&gt; a92ad73|0710d11 -> a92ad73'
        $markdown | Should Match '990946c -&gt; a92ad73|990946c -> a92ad73'
        $markdown | Should Match 'wrong_x'
        $markdown | Should Match 'p95_output_delta'
        $summary.comparisons.Count | Should Be 2
    }
}
