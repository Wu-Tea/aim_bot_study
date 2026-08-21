param(
    [string]$BuildDirectory = "native/vision_native/build",
    [string]$OutputDirectory = "runs/native_perf/runtime_acceptance/latest",
    [string]$RuntimeConfig = "config.toml"
)

$ErrorActionPreference = "Stop"
$release = Join-Path $BuildDirectory "Release"
$checks = @(
    @{ name = "base"; executable = "cod_native_base_tests.exe" },
    @{ name = "functional"; executable = "cod_native_functional_tests.exe" }
)

$results = @{}
foreach ($check in $checks) {
    $path = Join-Path $release $check.executable
    if (-not (Test-Path -LiteralPath $path)) {
        $results[$check.name] = @{
            status = "UNVERIFIED"
            executable = $check.executable
            reason = "binary missing"
        }
        continue
    }
    & $path
    $results[$check.name] = if ($LASTEXITCODE -eq 0) {
        @{ status = "PASS"; executable = $check.executable; exit_code = 0 }
    } else {
        @{ status = "FAIL"; executable = $check.executable; exit_code = $LASTEXITCODE }
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (
    Join-Path $OutputDirectory "focused-tests.json")
$focusedFailureCount = @(
    $results.Values | Where-Object { $_.status -ne "PASS" }
).Count
& "scripts/verify/native_pipeline_contract.bat" `
    -BuildDir $BuildDirectory `
    -RuntimeConfig $RuntimeConfig
$contractExit = $LASTEXITCODE
$contract = @{
    status = if ($contractExit -eq 0) { "PASS" } else { "FAIL" }
    exit_code = $contractExit
}
$contract | ConvertTo-Json | Set-Content -Encoding UTF8 (
    Join-Path $OutputDirectory "pipeline-contract.json")
if ($focusedFailureCount -ne 0) {
    throw "One or more native test runners failed or were not verified."
}
if ($contractExit -ne 0) {
    throw "Native pipeline contract failed with exit code $contractExit."
}
