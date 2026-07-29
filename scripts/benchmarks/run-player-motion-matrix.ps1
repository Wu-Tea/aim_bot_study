param(
    [string]$BuildDirectory = "b",
    [string]$ConfigPath = "config.native.example.toml",
    [string]$OutputDirectory = "artifacts/benchmarks/player-motion-20260729",
    [string]$Revision = "unknown",
    [int]$DurationMs = 60000,
    [uint32[]]$Seeds = @(2026072901, 2026072902, 2026072903)
)

$ErrorActionPreference = "Stop"
$executable = Join-Path $BuildDirectory "Release/cod_native_sustained_aimlab_benchmark.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Benchmark executable not found: $executable"
}

$seedArguments = @()
foreach ($seed in $Seeds) {
    $seedArguments += @("--seed", $seed.ToString())
}

$commonArguments = @(
    "--config", $ConfigPath
) + $seedArguments + @(
    "--profile", "both",
    "--cohort", "both",
    "--scenario", "baseline",
    "--target-profile", "ordinary",
    "--intent-fusion", "vector",
    "--duration-ms", $DurationMs.ToString(),
    "--revision", $Revision
)

$playerCases = @(
    @{ Name = "off"; Left = "off"; Vertical = "off" },
    @{ Name = "strafe"; Left = "full-reversal"; Vertical = "off" },
    @{ Name = "slide"; Left = "off"; Vertical = "slide" },
    @{ Name = "jump"; Left = "off"; Vertical = "jump" },
    @{ Name = "combined"; Left = "full-reversal"; Vertical = "random" }
)

foreach ($case in $playerCases) {
    foreach ($targetMotion in @("stationary", "moving")) {
        $output = Join-Path(
            $OutputDirectory,
            "$($case.Name)_$targetMotion.json")
        if (Test-Path -LiteralPath $output) {
            throw "Refusing to overwrite benchmark artifact: $output"
        }
        Write-Host "Running $($case.Name) / $targetMotion"
        & $executable @commonArguments `
            --left-strafe $case.Left `
            --vertical-motion $case.Vertical `
            --target-motion $targetMotion `
            --output $output
        if ($LASTEXITCODE -ne 0) {
            throw "Benchmark failed for $($case.Name) / $targetMotion"
        }
    }
}
