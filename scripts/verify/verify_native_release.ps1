[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedRoot = (Resolve-Path -LiteralPath $ReleaseRoot).Path
$manifestPath = Join-Path $resolvedRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Missing manifest: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema_version -ne 1) {
    throw "Unsupported manifest schema: $($manifest.schema_version)"
}
if ([string]$manifest.source_commit -notmatch '^[0-9a-f]{40}$') {
    throw "Invalid source commit in manifest: $($manifest.source_commit)"
}

$rootPrefix = $resolvedRoot.TrimEnd('\') + '\'
foreach ($file in $manifest.files) {
    $relative = [string]$file.path
    $nativeRelative = $relative.Replace('/', '\')
    if ([System.IO.Path]::IsPathRooted($nativeRelative) -or $nativeRelative.Split('\') -contains '..') {
        throw "Unsafe manifest path: $relative"
    }
    $path = [System.IO.Path]::GetFullPath((Join-Path $resolvedRoot $nativeRelative))
    if (-not $path.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Manifest path escapes release root: $relative"
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing $relative"
    }
    if ((Get-Item -LiteralPath $path).Length -ne [int64]$file.size) {
        throw "Size mismatch $relative"
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne [string]$file.sha256) {
        throw "Hash mismatch $relative"
    }
}

$modelPath = Join-Path $resolvedRoot ([string]$manifest.model_path).Replace('/', '\')
if (-not (Test-Path -LiteralPath $modelPath -PathType Leaf)) {
    throw "Configured engine missing from release: $modelPath"
}

Push-Location $resolvedRoot
try {
    $oldPrintOnly = $env:GAMEPAD_START_PRINT_ONLY
    $oldFireChoice = $env:GAMEPAD_START_FIRE_CHOICE_OVERRIDE
    $oldRecoilChoice = $env:GAMEPAD_START_RECOIL_CHOICE_OVERRIDE
    try {
        $env:GAMEPAD_START_PRINT_ONLY = '1'
        $env:GAMEPAD_START_FIRE_CHOICE_OVERRIDE = '1'
        $env:GAMEPAD_START_RECOIL_CHOICE_OVERRIDE = '1'
        & cmd.exe /d /c 'scripts\launch\gamepad_start.bat'
        if ($LASTEXITCODE -ne 0) {
            throw "Launcher resolution failed with exit code $LASTEXITCODE"
        }
    } finally {
        $env:GAMEPAD_START_PRINT_ONLY = $oldPrintOnly
        $env:GAMEPAD_START_FIRE_CHOICE_OVERRIDE = $oldFireChoice
        $env:GAMEPAD_START_RECOIL_CHOICE_OVERRIDE = $oldRecoilChoice
    }

    $oldTelemetryDirectory = $env:VISION_AIM_PERF_LOG_DIR
    $smokeTelemetryDirectory = Join-Path `
        ([System.IO.Path]::GetTempPath()) `
        ("cod_native_release_smoke_" + [System.Guid]::NewGuid().ToString('N'))
    try {
        $env:VISION_AIM_PERF_LOG_DIR = $smokeTelemetryDirectory
        & '.\native\vision_native\build\Release\cod_native_runtime.exe' --config config.toml --once
        if ($LASTEXITCODE -ne 0) {
            throw "Runtime smoke failed with exit code $LASTEXITCODE"
        }
    } finally {
        $env:VISION_AIM_PERF_LOG_DIR = $oldTelemetryDirectory
        if (Test-Path -LiteralPath $smokeTelemetryDirectory) {
            Remove-Item -LiteralPath $smokeTelemetryDirectory -Recurse -Force
        }
    }
} finally {
    Pop-Location
}

Write-Output "[NativeReleaseVerify] PASS root=$resolvedRoot files=$(@($manifest.files).Count)"
