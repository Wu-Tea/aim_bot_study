param(
    [switch]$BuildFirst,
    [switch]$SkipPythonTests,
    [switch]$SkipPipelineContract,
    [string]$BuildDirectory = "native\vision_native\build",
    [string]$RuntimeConfig = "config.toml"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $ProjectRoot

function Invoke-Checked {
    param(
        [string]$Label,
        [scriptblock]$Command
    )

    Write-Host "[NativeCppGamepadCheck] $Label"
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "[NativeCppGamepadCheck] $Label failed with exit code $LASTEXITCODE"
    }
}

function Invoke-DefaultLauncherPrintOnlyCheck {
    $previousPrintOnly = [Environment]::GetEnvironmentVariable("GAMEPAD_START_PRINT_ONLY", "Process")
    $previousFireChoice = [Environment]::GetEnvironmentVariable("GAMEPAD_START_FIRE_CHOICE_OVERRIDE", "Process")
    $previousRecoilChoice = [Environment]::GetEnvironmentVariable("GAMEPAD_START_RECOIL_CHOICE_OVERRIDE", "Process")
    $previousRuntime = [Environment]::GetEnvironmentVariable("GAMEPAD_RUNTIME", "Process")

    try {
        $env:GAMEPAD_START_PRINT_ONLY = "1"
        $env:GAMEPAD_START_FIRE_CHOICE_OVERRIDE = "2"
        $env:GAMEPAD_START_RECOIL_CHOICE_OVERRIDE = "1"
        Remove-Item Env:\GAMEPAD_RUNTIME -ErrorAction SilentlyContinue

        $output = & cmd /c "scripts\launch\gamepad_start.bat" 2>&1
        $text = ($output -join "`n")
        Write-Host $text
        if ($LASTEXITCODE -ne 0) {
            throw "[NativeCppGamepadCheck] default gamepad launcher failed with exit code $LASTEXITCODE"
        }
        if ($text -notmatch "cod_native_runtime\.exe") {
            throw "[NativeCppGamepadCheck] default gamepad launcher did not resolve cod_native_runtime.exe"
        }
        if ($text -match "main\.py --controller-mode gamepad") {
            throw "[NativeCppGamepadCheck] default gamepad launcher resolved the Python fallback"
        }
    } finally {
        if ($null -eq $previousPrintOnly) { Remove-Item Env:\GAMEPAD_START_PRINT_ONLY -ErrorAction SilentlyContinue } else { $env:GAMEPAD_START_PRINT_ONLY = $previousPrintOnly }
        if ($null -eq $previousFireChoice) { Remove-Item Env:\GAMEPAD_START_FIRE_CHOICE_OVERRIDE -ErrorAction SilentlyContinue } else { $env:GAMEPAD_START_FIRE_CHOICE_OVERRIDE = $previousFireChoice }
        if ($null -eq $previousRecoilChoice) { Remove-Item Env:\GAMEPAD_START_RECOIL_CHOICE_OVERRIDE -ErrorAction SilentlyContinue } else { $env:GAMEPAD_START_RECOIL_CHOICE_OVERRIDE = $previousRecoilChoice }
        if ($null -eq $previousRuntime) { Remove-Item Env:\GAMEPAD_RUNTIME -ErrorAction SilentlyContinue } else { $env:GAMEPAD_RUNTIME = $previousRuntime }
    }
}

function Assert-BinaryTextAbsent {
    param(
        [string]$Path,
        [string[]]$Forbidden,
        [string]$Context
    )

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $text = [System.Text.Encoding]::ASCII.GetString($bytes).ToLowerInvariant()
    foreach ($needle in $Forbidden) {
        if ($text.Contains($needle.ToLowerInvariant())) {
            throw "[NativeCppGamepadCheck] $Context contains forbidden binary text: $needle"
        }
    }
}

function Assert-TextAbsent {
    param(
        [string]$Path,
        [string[]]$Forbidden,
        [string]$Context
    )

    $text = (Get-Content -Raw $Path).ToLowerInvariant()
    foreach ($needle in $Forbidden) {
        if ($text.Contains($needle.ToLowerInvariant())) {
            throw "[NativeCppGamepadCheck] $Context contains forbidden text: $needle"
        }
    }
}

function Assert-DirectoryTextAbsent {
    param(
        [string[]]$Roots,
        [string[]]$Forbidden,
        [string]$Context
    )

    foreach ($root in $Roots) {
        Get-ChildItem -Path $root -Recurse -File -Include *.cpp,*.h |
            Where-Object {
                $_.Name -notlike "*_tests.cpp" -and
                $_.Name -notlike "*_incident_regression.cpp" -and
                $_.Name -notlike "*_benchmark.cpp"
            } |
            ForEach-Object {
            $text = (Get-Content -Raw $_.FullName).ToLowerInvariant()
            foreach ($needle in $Forbidden) {
                if ($text.Contains($needle.ToLowerInvariant())) {
                    throw "[NativeCppGamepadCheck] $Context contains forbidden text '$needle' in $($_.FullName)"
                }
            }
        }
    }
}

if ($BuildFirst) {
    Invoke-Checked "build native vision/runtime" {
        powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1 `
            -BuildDir $BuildDirectory
    }
}

$BuildPath = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
} else {
    Join-Path $ProjectRoot $BuildDirectory
}
$RuntimeConfigPath = if ([System.IO.Path]::IsPathRooted($RuntimeConfig)) {
    [System.IO.Path]::GetFullPath($RuntimeConfig)
} else {
    Join-Path $ProjectRoot $RuntimeConfig
}
$RuntimeExe = Join-Path $BuildPath "Release\cod_native_runtime.exe"
$BaseTestsExe = Join-Path $BuildPath "Release\cod_native_base_tests.exe"
$NativeLauncher = Join-Path $ProjectRoot "scripts\launch\gamepad_native_cpp_start.bat"
$NativeRuntimeSourceRoots = @(
    (Join-Path $ProjectRoot "native\runtime_app"),
    (Join-Path $ProjectRoot "native\controller_native")
)

if (-not (Test-Path $RuntimeExe)) {
    throw "[NativeCppGamepadCheck] cod_native_runtime.exe not found. Run with -BuildFirst first."
}
if (-not (Test-Path $BaseTestsExe)) {
    throw "[NativeCppGamepadCheck] cod_native_base_tests.exe not found. Run with -BuildFirst first."
}
if (-not (Test-Path $NativeLauncher)) {
    throw "[NativeCppGamepadCheck] gamepad_native_cpp_start.bat not found."
}

if (-not $SkipPipelineContract) {
    Invoke-Checked "native tracker/controller/recoil pipeline contract" {
        powershell -ExecutionPolicy Bypass `
            -File scripts\verify\native_pipeline_contract.ps1 `
            -BuildDir $BuildDirectory `
            -RuntimeConfig $RuntimeConfigPath `
            -SkipBuild `
            -SkipBenchmark
    }
}

Invoke-Checked "native BaseEndToEnd contract tests" {
    & $BaseTestsExe --suite BaseEndToEnd
}

Invoke-Checked "native runtime one-tick smoke" {
    & $RuntimeExe --config $RuntimeConfigPath --perf-log --once
}

Invoke-Checked "native runtime short sustained smoke" {
    & $RuntimeExe --config $RuntimeConfigPath --perf-log --max-ticks 60
}

Invoke-Checked "runtime binary has no Python dependency" {
    Assert-BinaryTextAbsent `
        -Path $RuntimeExe `
        -Forbidden @("python", "pybind") `
        -Context "cod_native_runtime.exe"
}

Invoke-Checked "native launcher has no Python gameplay dependency" {
    Assert-TextAbsent `
        -Path $NativeLauncher `
        -Forbidden @("py -3", "python", "main.py", "recoil_runtime_launcher.py", "--controller-mode gamepad") `
        -Context "gamepad_native_cpp_start.bat"
}

Invoke-Checked "native runtime source has no Python gameplay dependency" {
    Assert-DirectoryTextAbsent `
        -Roots $NativeRuntimeSourceRoots `
        -Forbidden @("python", "pybind", "main.py", "recoil_runtime_launcher.py", "--controller-mode gamepad") `
        -Context "native runtime source"
}

Invoke-Checked "default gamepad launcher resolves native runtime" {
    Invoke-DefaultLauncherPrintOnlyCheck
}

if (-not $SkipPythonTests) {
    Invoke-Checked "Python startup regression" {
        py -3 -B -m unittest `
            tests.test_startup_scripts `
            -v
    }
}

Write-Host "[NativeCppGamepadCheck] PASS"
