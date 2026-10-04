param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [string]$BaselineDir="",
    [switch]$IndependentValidation
)
$ErrorActionPreference="Stop"
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$Config=(Resolve-Path -LiteralPath $Config).Path
if(Test-Path -LiteralPath $OutputDir) { throw "new output directory required" }
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$OutputDir=(Resolve-Path -LiteralPath $OutputDir).Path
$seeds=@('20261003','8675309','2026100301','3141592653')
if($IndependentValidation) { $seeds=@('3141592667','2026100311','2026100317','104729') }
$profiles=@('pure','mixed','wrong-then-correct','scripted')
$arms=@(
    @{Name='standard';Duration=10000;Edge='0.50';Center='0.40';Extra=@()},
    @{Name='slow';Duration=10000;Edge='0.35';Center='0.20';Extra=@()},
    @{Name='delayed';Duration=10000;Edge='0.35';Center='0.20';Extra=@('--vision-hz','100','--vision-result-delay-ms','8','--control-response-delay-ms','9')},
    @{Name='long';Duration=60000;Edge='0.50';Center='0.40';Extra=@()}
)
$identity=@{
    schema='target-hold-protected-matrix-v1';seeds=$seeds;profiles=$profiles;arms=$arms;
    config_sha256=(Get-FileHash -LiteralPath $Config -Algorithm SHA256).Hash.ToLower();
    target_slot_ms=1575;controller_hz=1000;cohort='both';
    policy_sha256=(Get-FileHash -LiteralPath (Join-Path $root 'docs/benchmarks/sustained-aimlab-optimization-policy-v1.json') -Algorithm SHA256).Hash.ToLower()
}
if($BaselineDir) {
    $prior=Get-Content -LiteralPath (Join-Path $BaselineDir 'contract.json') -Raw | ConvertFrom-Json
    # JSON object member order is not a covariate. Hashtable serialization
    # order may differ between PowerShell processes; compare JSON trees.
    $left=[System.Text.Json.Nodes.JsonNode]::Parse(($prior|ConvertTo-Json -Depth 10 -Compress))
    $right=[System.Text.Json.Nodes.JsonNode]::Parse(($identity|ConvertTo-Json -Depth 10 -Compress))
    if(-not [System.Text.Json.Nodes.JsonNode]::DeepEquals($left,$right)) { throw "frozen matrix identity differs" }
}
$identity|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDir 'contract.json') -Encoding utf8
$rows=@()
$failedPacks=0
foreach($profile in $profiles) { foreach($arm in $arms) {
    $name=$profile+'_'+$arm.Name
    $output=Join-Path $OutputDir ($name+'.json')
    $arguments=@('--config',$Config,'--output',$output,'--duration-ms',[string]$arm.Duration,
        '--controller-tick-hz','1000','--target-slot-ms','1575','--profile',$profile,
        '--cohort','both','--slowdown-edge',$arm.Edge,'--slowdown-center',$arm.Center,'--dirty','--smoke')
    $arguments+=$arm.Extra
    foreach($seed in $seeds) { $arguments+=@('--seed',$seed) }
    & $Executable @arguments | Set-Content -LiteralPath (Join-Path $OutputDir ($name+'.log'))
    if($LASTEXITCODE -ne 0) { throw "native matrix failed: $name" }
    $result=$null
    if($BaselineDir) {
        $result=& (Join-Path $PSScriptRoot 'compare_sustained_aimlab.ps1') `
            -Baseline (Join-Path $BaselineDir ($name+'.json')) -Candidate $output -ReportOnly
        $result|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $OutputDir ($name+'.comparison.json')) -Encoding utf8
        if(-not ($result -contains 'AIMLAB_CONSTRAINT_GATE=BENCHMARK-ELIGIBLE')) { $failedPacks++ }
    }
    $rows+=@{name=$name;argv=$arguments;comparison=$result;
        gate=if(-not $BaselineDir) {'baseline'} elseif($result -contains 'AIMLAB_CONSTRAINT_GATE=BENCHMARK-ELIGIBLE') {'eligible'} else {'failed'}}
    Write-Output "completed $name"
} }
@{identity=$identity;executable_sha256=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLower();packs=$rows} |
    ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $OutputDir 'matrix.json') -Encoding utf8
if($failedPacks) { throw "$failedPacks protected matrix pack(s) failed; candidate is not eligible" }
