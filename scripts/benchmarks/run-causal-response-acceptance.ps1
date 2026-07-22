param(
    [string]$BuildDir = "native/vision_native/build",
    [ValidateSet("Shadow", "RolloutShadow")]
    [string]$Mode = "RolloutShadow",
    [int[]]$Seeds = @(1337, 7331, 20260722),
    [string[]]$RealSession = @()
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $repo $BuildDir
}
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
$revision = (& git -C $repo rev-parse --short HEAD).Trim()
$sourceStatus = @(& git -C $repo status --porcelain | Where-Object {
    $_ -notmatch 'artifacts/benchmarks/causal-response/'
})
$initialDirty = [bool]$sourceStatus.Count
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$output = Join-Path $repo "artifacts/benchmarks/causal-response/$timestamp-$revision"
New-Item -ItemType Directory -Force $output | Out-Null

& $cmake --build $build --config Release --target `
    cod_native_causal_response_benchmark `
    cod_native_causal_response_benchmark_tests `
    cod_native_causal_online_response_learner_tests `
    cod_native_pending_motion_model_tests `
    cod_native_short_horizon_rollout_tests `
    cod_native_causal_response_microbenchmark
if ($LASTEXITCODE -ne 0) { throw "causal response build failed" }

& $ctest --test-dir $build -C Release `
    -R "CausalResponseSynthetic|CausalOnlineResponse|PendingMotion|ShortHorizonRollout" `
    --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "focused causal response tests failed" }

$benchmark = Join-Path $build "Release/cod_native_causal_response_benchmark.exe"
$microbenchmark = Join-Path $build "Release/cod_native_causal_response_microbenchmark.exe"
$seedReports = @()
foreach ($seed in $Seeds) {
    $first = Join-Path $output "seed-$seed-a.json"
    $second = Join-Path $output "seed-$seed-b.json"
    & $benchmark --seed $seed --output $first
    & $benchmark --seed $seed --output $second
    if ((Get-FileHash $first -Algorithm SHA256).Hash -ne
        (Get-FileHash $second -Algorithm SHA256).Hash) {
        throw "seed $seed is not byte deterministic"
    }
    $report = Get-Content -Raw $first | ConvertFrom-Json
    if (($report.mutations | Where-Object { -not $_.detected }).Count -ne 0) {
        throw "seed $seed failed a retained mutation"
    }
    if ($report.rollout.harmful_release_count -ne 0) {
        throw "seed $seed increased harmful release"
    }
    $seedReports += [ordered]@{
        seed = $seed
        decisions = $report.rollout.decisions
        top1_agreement = $report.rollout.top1_agreement
        causal_gain_px_ms = $report.rollout.causal_gain_px_ms
        regret_px_ms = $report.rollout.regret_px_ms
        harmful_release_count = $report.rollout.harmful_release_count
        single_target_aggressive_gain_px_ms =
            $report.single_target_aggressive_gain_px_ms
        multi_target_aggressive_regret_px_ms =
            $report.multi_target_aggressive_regret_px_ms
    }
}

$cpuPath = Join-Path $output "cpu.json"
& $microbenchmark | Set-Content -Encoding UTF8 $cpuPath
$cpu = Get-Content -Raw $cpuPath | ConvertFrom-Json
if ($cpu.history_push_us.p95 -ge 10.0 -or $cpu.history_push_us.p99 -ge 25.0) {
    throw "controller history CPU gate failed"
}
if ($cpu.vision_observe_estimate_us.p95 -ge 150.0) {
    throw "vision observe+estimate CPU gate failed"
}

$configPath = Join-Path $repo "config.native.example.toml"
$configHash = (Get-FileHash $configPath -Algorithm SHA256).Hash.ToLowerInvariant()
$realCoverage = @()
foreach ($session in $RealSession) {
    $resolved = Resolve-Path $session
    $realCoverage += [ordered]@{
        path = $resolved.Path
        status = "replay_requires_fresh_g0_session"
    }
}
$summary = [ordered]@{
    schema = "causal_response_acceptance_v1"
    status = "synthetic_pass_real_replay_pending"
    mode = $Mode
    revision = $revision
    dirty = $initialDirty
    config_path = $configPath
    config_sha256 = $configHash
    engine_sha256 = "unavailable_in_worktree"
    crop = [ordered]@{ width = 480; height = 416 }
    telemetry_schema = 6
    seeds = $seedReports
    cpu = $cpu
    real_sessions = $realCoverage
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 `
    (Join-Path $output "acceptance-summary.json")
Write-Output $output
