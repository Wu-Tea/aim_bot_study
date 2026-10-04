[CmdletBinding()]
param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$dependencyRoot = Join-Path $projectRoot 'artifacts/mouse_link/deps'
$archive = Join-Path $dependencyRoot 'Interception-v1.0.1.zip'
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri 'https://github.com/oblitum/Interception/releases/download/v1.0.1/Interception.zip' -OutFile $archive
}
$package = Join-Path $dependencyRoot 'Interception'
if (-not (Test-Path -LiteralPath $package)) {
    Expand-Archive -LiteralPath $archive -DestinationPath $dependencyRoot
}
$cmake = 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$build = Join-Path $projectRoot 'artifacts/mouse_link/build'
& $cmake -S (Join-Path $projectRoot 'native/mouse_link') -B $build -G 'Visual Studio 17 2022' -A x64 "-DINTERCEPTION_ROOT=$package"
if ($LASTEXITCODE -ne 0) { throw 'Mouse link configure failed.' }
& $cmake --build $build --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Mouse link build failed.' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $build -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Mouse link tests failed.' }
Write-Output "Tool: $build/$Configuration/mouse_link_tool.exe"
Write-Output 'No driver was installed and no physical input was intercepted by these tests.'
