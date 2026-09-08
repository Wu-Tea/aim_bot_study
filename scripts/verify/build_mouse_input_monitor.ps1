[CmdletBinding()]
param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$cmake = 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$build = Join-Path $projectRoot 'artifacts/mouse_input_monitor/build'
& $cmake -S (Join-Path $projectRoot 'native/mouse_link') -B $build -G 'Visual Studio 17 2022' -A x64 -DMOUSE_LISTENER_ONLY=ON
if ($LASTEXITCODE -ne 0) { throw 'Listener configure failed.' }
& $cmake --build $build --config $Configuration --target mouse_input_monitor mouse_input_provenance_tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Listener build failed.' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $build -C $Configuration --output-on-failure -R '^MouseInputProvenance$'
if ($LASTEXITCODE -ne 0) { throw 'Listener evidence classification tests failed.' }
Write-Output "Listener: $build/$Configuration/mouse_input_monitor.exe"
Write-Output 'No capture, driver installation, or SendInput experiment performed by this build script.'
