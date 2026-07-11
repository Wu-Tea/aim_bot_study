param(
    [string]$BuildDirectory = "native/vision_native/build",
    [string]$OutputDirectory = "runs/native_perf/runtime_acceptance/latest"
)

$ErrorActionPreference = "Stop"
$release = Join-Path $BuildDirectory "Release"
$tests = @(
    "cod_native_runtime_config_tests.exe",
    "cod_native_vision_service_tests.exe",
    "cod_native_runtime_timing_tests.exe",
    "cod_native_runtime_telemetry_tests.exe",
    "vision_native_color_readback_tests.exe",
    "vision_native_build_family_tests.exe",
    "cod_native_target_selector_tests.exe",
    "cod_native_controller_tests.exe",
    "cod_native_recoil_contract_tests.exe"
)

$results = @{}
foreach ($test in $tests) {
    $path = Join-Path $release $test
    if (-not (Test-Path -LiteralPath $path)) {
        $results[$test] = @{ status = "UNVERIFIED"; reason = "binary missing" }
        continue
    }
    & $path
    $results[$test] = if ($LASTEXITCODE -eq 0) {
        @{ status = "PASS"; exit_code = 0 }
    } else {
        @{ status = "FAIL"; exit_code = $LASTEXITCODE }
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (
    Join-Path $OutputDirectory "focused-tests.json")
& "scripts/verify/native_pipeline_contract.bat"
$contract = @{ status = if ($LASTEXITCODE -eq 0) { "PASS" } else { "FAIL" }; exit_code = $LASTEXITCODE }
$contract | ConvertTo-Json | Set-Content -Encoding UTF8 (
    Join-Path $OutputDirectory "pipeline-contract.json")
