param(
    [string]$BuildDir = "native\vision_native\build",
    [string]$Configuration = "Release",
    [string]$RuntimeConfig = "config.toml",
    [switch]$SkipBuild,
    [switch]$SkipBenchmark
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $repoRoot

function Resolve-CMake {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmakeCommand) {
        return $cmakeCommand.Source
    }

    $vsCMake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $vsCMake) {
        return $vsCMake
    }

    throw "cmake was not found in PATH or at the Visual Studio bundled path."
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Assert-NoForbiddenCoupling {
    $runtimeConfigForScan = if ([System.IO.Path]::IsPathRooted($RuntimeConfig)) {
        $RuntimeConfig
    } else {
        Join-Path $repoRoot $RuntimeConfig
    }
    $boundaryFiles = @(
        "native\controller_native\recoil_compensation.h",
        "native\controller_native\recoil_compensation.cpp",
        "native\controller_native\runtime_config.h",
        "native\controller_native\runtime_config.cpp",
        "native\controller_native\native_gamepad_controller.cpp",
        $runtimeConfigForScan
    ) | Where-Object { Test-Path $_ }
    $recoilFiles = @(
        "native\controller_native\recoil_compensation.h",
        "native\controller_native\recoil_compensation.cpp"
    )
    $controllerFiles = @(
        "native\controller_native\native_gamepad_controller.cpp"
    )
    $controllerPublicHeaders = @(
        "native\controller_native\native_gamepad_controller.h"
    )
    $controllerProductionFiles = Get-ChildItem -Path "native\controller_native" -Include "*.h", "*.cpp" -Recurse |
        Where-Object {
            $_.Name -notlike "*_tests.cpp" -and
            $_.Name -notlike "*_benchmark.cpp" -and
            $_.Name -notlike "*_benchmark.h"
        }
    $trackingHeaders = Get-ChildItem -Path "native\tracking_native" -Include "*.h" -Recurse
    $cmakeLists = "native\vision_native\CMakeLists.txt"
    $forbiddenBoundaryPatterns = @(
        "target_direction_yield",
        "target_observed_at_seconds",
        "tracker_ego_motion_includes_recoil",
        "includes_recoil",
        "pre_recoil",
        "post_recoil"
    )

    foreach ($pattern in $forbiddenBoundaryPatterns) {
        $matches = Select-String -Path $boundaryFiles -Pattern $pattern -ErrorAction SilentlyContinue
        if ($matches) {
            $details = ($matches | ForEach-Object {
                "$($_.Path):$($_.LineNumber): $($_.Line.Trim())"
            }) -join "`n"
            throw "Forbidden recoil/controller coupling pattern found: $pattern`n$details"
        }
    }

    foreach ($pattern in @("\btarget_dx\b", "\btarget_dy\b")) {
        $matches = Select-String -Path $recoilFiles -Pattern $pattern -ErrorAction SilentlyContinue
        if ($matches) {
            $details = ($matches | ForEach-Object {
                "$($_.Path):$($_.LineNumber): $($_.Line.Trim())"
            }) -join "`n"
            throw "Forbidden recoil target feedback field found: $pattern`n$details"
        }
    }

    foreach ($pattern in @("input\.target_dx", "input\.target_dy")) {
        $matches = Select-String -Path $controllerFiles -Pattern $pattern -ErrorAction SilentlyContinue
        if ($matches) {
            $details = ($matches | ForEach-Object {
                "$($_.Path):$($_.LineNumber): $($_.Line.Trim())"
            }) -join "`n"
            throw "Forbidden controller-to-recoil target feedback assignment found: $pattern`n$details"
        }
    }

    $matches = Select-String -Path $controllerPublicHeaders -Pattern "vision_native" -ErrorAction SilentlyContinue
    if ($matches) {
        $details = ($matches | ForEach-Object {
            "$($_.Path):$($_.LineNumber): $($_.Line.Trim())"
        }) -join "`n"
        throw "Forbidden controller public header dependency on vision_native found.`n$details"
    }

    $matches = Select-String -Path $controllerProductionFiles.FullName -Pattern "vision_native" -ErrorAction SilentlyContinue
    if ($matches) {
        $details = ($matches | ForEach-Object {
            "$($_.Path):$($_.LineNumber): $($_.Line.Trim())"
        }) -join "`n"
        throw "Forbidden controller production dependency on vision_native found.`n$details"
    }

    $matches = Select-String -Path $trackingHeaders.FullName -Pattern "controller_native" -ErrorAction SilentlyContinue
    if ($matches) {
        $details = ($matches | ForEach-Object {
            "$($_.Path):$($_.LineNumber): $($_.Line.Trim())"
        }) -join "`n"
        throw "Forbidden tracking header dependency on controller_native found.`n$details"
    }

    $requiredSplitTestFiles = @(
        "native\vision_native\src\target_selector_tests.cpp",
        "native\controller_native\controller_protocol_tests.cpp",
        "native\controller_native\recoil_contract_tests.cpp",
        "native\controller_native\weapon_recognizer_tests.cpp",
        "native\controller_native\benchmark_metrics_tests.cpp"
    )
    foreach ($testFile in $requiredSplitTestFiles) {
        if (-not (Test-Path $testFile)) {
            throw "Required split test file not found: $testFile"
        }
    }

    $requiredSplitTargets = @(
        "cod_native_target_selector_tests",
        "cod_native_controller_protocol_tests",
        "cod_native_recoil_contract_tests",
        "cod_native_weapon_recognizer_tests",
        "cod_native_benchmark_metrics_tests"
    )
    foreach ($targetName in $requiredSplitTargets) {
        $targetMatch = Select-String -Path $cmakeLists -Pattern $targetName -SimpleMatch -ErrorAction SilentlyContinue
        if (-not $targetMatch) {
            throw "Required split test target not found in CMakeLists.txt: $targetName"
        }
    }

}

$cmake = Resolve-CMake
$buildPath = Join-Path $repoRoot $BuildDir
$runtimeConfigPath = if ([System.IO.Path]::IsPathRooted($RuntimeConfig)) {
    [System.IO.Path]::GetFullPath($RuntimeConfig)
} else {
    Join-Path $repoRoot $RuntimeConfig
}
$artifactDir = Join-Path $repoRoot "runs\native_pipeline_contract"
if (-not (Test-Path -LiteralPath $runtimeConfigPath)) {
    $worktreeRoots = @(
        & git worktree list --porcelain |
            Where-Object { $_ -like "worktree *" } |
            ForEach-Object { $_.Substring(9) }
    )
    $modelPath = $worktreeRoots |
        ForEach-Object { Join-Path $_ "models\candidates\body_union_manual_core_x2_neg_e6_640x512.engine" } |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if (-not $modelPath) {
        throw "Runtime config is missing and no verification engine was found in any Git worktree."
    }
    New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
    $runtimeConfigPath = Join-Path $artifactDir "runtime_config.toml"
    @(
        "[runtime]",
        'profile = "balanced"',
        "[runtime.vision]",
        ('model_path = "' + ($modelPath -replace '\\', '/') + '"'),
        "[runtime.output]",
        "enabled = false"
    ) | Set-Content -Encoding ASCII -LiteralPath $runtimeConfigPath
}
$runtimeWorkingDir = Split-Path -Parent $runtimeConfigPath
$runtimeExe = Join-Path $buildPath "$Configuration\cod_native_runtime.exe"
$testsExe = Join-Path $buildPath "$Configuration\cod_native_controller_tests.exe"
$benchmarkExe = Join-Path $buildPath "$Configuration\cod_native_sustained_aimlab_benchmark.exe"
$benchmarkOutput = Join-Path $artifactDir "pipeline_contract_smoke.json"

Assert-NoForbiddenCoupling

if (-not $SkipBuild) {
    Invoke-Checked $cmake @("--build", $BuildDir, "--config", $Configuration, "--target", "cod_native_controller_tests")
    Invoke-Checked $cmake @("--build", $BuildDir, "--config", $Configuration, "--target", "cod_native_runtime")
    if (-not $SkipBenchmark) {
        Invoke-Checked $cmake @("--build", $BuildDir, "--config", $Configuration, "--target", "cod_native_sustained_aimlab_benchmark")
    }
}

if (-not (Test-Path $testsExe)) {
    throw "Controller test executable not found: $testsExe"
}
if (-not (Test-Path $runtimeExe)) {
    throw "Native runtime executable not found: $runtimeExe"
}

$testOutput = & $testsExe 2>&1
$testExit = $LASTEXITCODE
$testOutput | ForEach-Object { Write-Output $_ }
if ($testExit -ne 0 -or (($testOutput -join "`n") -notmatch
        "\[(NativeControllerTests|TargetPipelineIntegrationTests)\] PASS")) {
    throw "Native controller contract tests failed."
}

Push-Location $runtimeWorkingDir
try {
    $runtimeOutput = & $runtimeExe --config $runtimeConfigPath --once 2>&1
    $runtimeExit = $LASTEXITCODE
} finally {
    Pop-Location
}
$runtimeOutput | ForEach-Object { Write-Output $_ }
if ($runtimeExit -ne 0) {
    throw "Native runtime smoke failed."
}
if (($runtimeOutput -join "`n") -notmatch "tracker_max_observation_age_ms=") {
    throw "Runtime summary did not report the current source-age authority gate."
}

if (-not $SkipBenchmark) {
    if (-not (Test-Path $benchmarkExe)) {
        throw "Native sustained AimLab benchmark executable not found: $benchmarkExe"
    }
    New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
    Invoke-Checked $benchmarkExe @(
        "--config", $runtimeConfigPath,
        "--duration-ms", "3000",
        "--profile", "pure",
        "--cohort", "both",
        "--vision-hz", "180",
        "--short-occlusion-ms", "36",
        "--target-motion", "moving",
        "--left-strafe", "both",
        "--smoke",
        "--output", $benchmarkOutput)
    if (-not (Test-Path $benchmarkOutput)) {
        throw "Benchmark artifact was not written: $benchmarkOutput"
    }
    $benchmarkReport = Get-Content -Raw -LiteralPath $benchmarkOutput |
        ConvertFrom-Json
    if ($benchmarkReport.control_path -ne "production-only") {
        throw "Benchmark did not report the current production-only control path."
    }
}

Write-Output "[NativePipelineContract] PASS"
