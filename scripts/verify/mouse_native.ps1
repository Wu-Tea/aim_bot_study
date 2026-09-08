[CmdletBinding()]
param([string]$Configuration = 'Release', [switch]$DesktopProbe)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$cmake = 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$build = Join-Path $projectRoot 'native/vision_native/build'
if (-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))) {
    throw 'Configure the native build with tools/build_native_vision.ps1 first.'
}
$targets = @('cod_native_mouse_runtime', 'cod_native_base_tests', 'cod_native_functional_tests')
if ($DesktopProbe) { $targets += 'cod_native_mouse_win32_debug_transport_tests' }
foreach ($suite in @('relay_contract', 'adapter', 'sensitivity_calibrator', 'controller_facade',
        'emergency_exit', 'controller_runtime_core', 'relay_client', 'auto_fire_button', 'tuning', 'interception_transport', 'manual_judgment',
        'virtual_hid_transport', 'virtual_session', 'runtime_supervisor', 'bodylock_deadzone', 'bodylock_escape', 'recoil', 'diagnostics', 'bodylock_curve', 'target_point', 'target_point_boundaries')) {
    $targets += "cod_native_mouse_${suite}_tests"
}
& $cmake --build $build --config $Configuration --target @targets --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Native mouse build failed.' }
& $ctest --test-dir $build -C $Configuration --output-on-failure -R '^(Mouse_|Base|Feature)'
if ($LASTEXITCODE -ne 0) { throw 'Native mouse or shared product regression failed.' }
Write-Output 'PASS: offline contracts. No mouse interception or driver installation was performed.'
if ($DesktopProbe) {
    & (Join-Path $build "$Configuration/cod_native_mouse_runtime.exe") --transport win32-debug --check-transport
    if ($LASTEXITCODE -ne 0) { throw 'Mouse transport registration probe failed.' }
    & (Join-Path $build "$Configuration/cod_native_mouse_win32_debug_transport_tests.exe")
    if ($LASTEXITCODE -ne 0) { throw 'Desktop injection/lifecycle probe failed.' }
    Write-Output 'PASS: desktop injection/lifecycle probe; physical-device/game acceptance remains separate.'
}
