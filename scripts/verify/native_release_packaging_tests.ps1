[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,

    [Parameter(Mandatory = $true)]
    [string]$ScratchRoot,

    [Parameter(Mandatory = $true)]
    [string]$SourceCommit
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$packageScript = Join-Path $repositoryRoot 'scripts\release\package_native_runtime.ps1'
$verifyScript = Join-Path $repositoryRoot 'scripts\verify\verify_native_release.ps1'

if (-not (Test-Path -LiteralPath $packageScript -PathType Leaf)) {
    throw "Missing package script: $packageScript"
}
if (-not (Test-Path -LiteralPath $verifyScript -PathType Leaf)) {
    throw "Missing verification script: $verifyScript"
}

$resolvedScratch = [System.IO.Path]::GetFullPath($ScratchRoot)
$scratchParent = Split-Path -Parent $resolvedScratch
if (-not (Test-Path -LiteralPath $scratchParent -PathType Container)) {
    New-Item -ItemType Directory -Path $scratchParent | Out-Null
}
if (Test-Path -LiteralPath $resolvedScratch) {
    Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
}

& $packageScript `
    -SourceRoot $SourceRoot `
    -OutputRoot $resolvedScratch `
    -SourceCommit $SourceCommit `
    -ReleaseTitle 'Native Runtime Packaging Test Release' `
    -ReleasePurpose 'Packaging metadata test purpose.' `
    -FallbackRollbackTag 'packaging-test-rollback-tag'
if ($LASTEXITCODE -ne 0) {
    throw "Package script exited with $LASTEXITCODE"
}

$manifestPath = Join-Path $resolvedScratch 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'Package must contain manifest.json.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.source_commit -ne $SourceCommit) {
    throw "Manifest commit mismatch: $($manifest.source_commit)"
}
if ($manifest.model_path -ne 'models/candidates/body_union_manual_core_x2_neg_e6_640x512.engine') {
    throw "Unexpected packaged model path: $($manifest.model_path)"
}

$readmeText = Get-Content -LiteralPath (Join-Path $resolvedScratch 'README.txt') -Raw
foreach ($expectedText in @(
    'Native Runtime Packaging Test Release',
    'Packaging metadata test purpose.',
    'Fallback rollback tag: packaging-test-rollback-tag'
)) {
    if (-not $readmeText.Contains($expectedText)) {
        throw "README is missing release metadata: $expectedText"
    }
}

$required = @(
    'config.toml',
    'scripts/launch/gamepad_start.bat',
    'scripts/launch/gamepad_native_cpp_start.bat',
    'native/vision_native/build/Release/cod_native_runtime.exe',
    'native/vision_native/build/Release/nvinfer_10.dll',
    'native/vision_native/build/Release/nvinfer_plugin_10.dll',
    'native/vision_native/build/Release/SDL2.dll',
    'native/vision_native/build/Release/ViGEmClient.dll',
    'models/candidates/body_union_manual_core_x2_neg_e6_640x512.engine',
    'README.txt'
)
$manifestPaths = @($manifest.files | ForEach-Object { [string]$_.path })
foreach ($relativePath in $required) {
    if ($relativePath -notin $manifestPaths) {
        throw "Required file absent from manifest: $relativePath"
    }
}

$forbidden = Get-ChildItem -LiteralPath $resolvedScratch -File -Recurse | Where-Object {
    $_.Extension -in @('.cpp', '.h', '.lib', '.pdb') -or
    $_.FullName -match '[\\/]runs[\\/]'
}
if ($forbidden) {
    throw "Package contains forbidden files: $($forbidden.FullName -join ', ')"
}

& $verifyScript -ReleaseRoot $resolvedScratch
if ($LASTEXITCODE -ne 0) {
    throw "Release verifier exited with $LASTEXITCODE"
}

$postVerifyActual = @(
    Get-ChildItem -LiteralPath $resolvedScratch -File -Recurse |
        ForEach-Object { $_.FullName.Substring($resolvedScratch.Length + 1).Replace('\', '/') }
)
$postVerifyExpected = @($manifestPaths) + 'manifest.json'
$postVerifyDelta = @(Compare-Object $postVerifyExpected $postVerifyActual)
if ($postVerifyDelta) {
    throw "Smoke verification must not mutate the release: $($postVerifyDelta | Out-String)"
}

Write-Output '[NativeReleasePackagingTests] PASS'
