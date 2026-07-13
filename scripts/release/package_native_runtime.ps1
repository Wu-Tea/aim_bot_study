[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$SourceCommit,

    [string]$ReleaseTitle = 'Native Runtime Pre-Tracker-Refactor Release',

    [string]$ReleasePurpose = @'
The package is frozen as the fallback while tracker/authority/bodylock refactoring
continues. Do not replace its executable or DLLs with later worktree builds.
'@,

    [string]$FallbackRollbackTag = 'pre-tracker-authority-refactor-20260713'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-TomlString {
    param(
        [Parameter(Mandatory = $true)][string]$Content,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][string]$ConfigPath
    )

    $pattern = '(?m)^\s*' + [regex]::Escape($Key) + '\s*=\s*["'']([^"'']+)["'']'
    $match = [regex]::Match($Content, $pattern)
    if (-not $match.Success) {
        throw "Missing TOML string '$Key' in $ConfigPath"
    }
    return $match.Groups[1].Value
}

function Read-TomlScalar {
    param(
        [Parameter(Mandatory = $true)][string]$Content,
        [Parameter(Mandatory = $true)][string]$Key
    )

    $quotedPattern = '(?m)^\s*' + [regex]::Escape($Key) + '\s*=\s*["'']([^"'']+)["'']'
    $quotedMatch = [regex]::Match($Content, $quotedPattern)
    if ($quotedMatch.Success) {
        return $quotedMatch.Groups[1].Value
    }
    $plainPattern = '(?m)^\s*' + [regex]::Escape($Key) + '\s*=\s*([^#\r\n]+)'
    $plainMatch = [regex]::Match($Content, $plainPattern)
    if ($plainMatch.Success) {
        return $plainMatch.Groups[1].Value.Trim()
    }
    return $null
}

function Normalize-RelativePath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return $Path.Replace('\', '/')
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$BasePath,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $baseUri = [System.Uri]::new(([System.IO.Path]::GetFullPath($BasePath).TrimEnd('\') + '\'))
    $pathUri = [System.Uri]::new([System.IO.Path]::GetFullPath($Path))
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}

$resolvedSource = (Resolve-Path -LiteralPath $SourceRoot).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputRoot)
$sourcePrefix = $resolvedSource.TrimEnd('\') + '\'
if (-not $resolvedOutput.StartsWith($sourcePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must stay inside SourceRoot. source=$resolvedSource output=$resolvedOutput"
}
if ($resolvedOutput -eq $resolvedSource) {
    throw 'OutputRoot cannot equal SourceRoot.'
}

$head = (& git -C $resolvedSource rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to read source repository HEAD.'
}
& git -C $resolvedSource cat-file -e "$SourceCommit^{commit}"
if ($LASTEXITCODE -ne 0) {
    throw "Source commit does not exist: $SourceCommit"
}
if ($head -ne $SourceCommit.ToLowerInvariant()) {
    $changedAfterSource = @(& git -C $resolvedSource diff --name-only $SourceCommit $head)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to compare source commit with HEAD.'
    }
    $disallowedChanges = @($changedAfterSource | Where-Object { $_ -ne '.gitignore' })
    if ($disallowedChanges) {
        throw "HEAD differs from frozen source in release inputs: $($disallowedChanges -join ', ')"
    }
}
$dirty = @(& git -C $resolvedSource status --porcelain)
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to read source repository status.'
}
if ($dirty) {
    throw "SourceRoot must be clean before packaging: $($dirty -join '; ')"
}

$configPath = Join-Path $resolvedSource 'config.toml'
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "Missing release config: $configPath"
}
$configText = Get-Content -LiteralPath $configPath -Raw
$modelRelative = Read-TomlString -Content $configText -Key 'model_path' -ConfigPath $configPath
$profileRelative = Read-TomlString -Content $configText -Key 'profile_directory' -ConfigPath $configPath
$calibrationRelative = Read-TomlString -Content $configText -Key 'calibration_directory' -ConfigPath $configPath
$weaponRelative = Read-TomlString -Content $configText -Key 'weapon_directory' -ConfigPath $configPath
$stateRelative = Read-TomlString -Content $configText -Key 'recognizer_state_path' -ConfigPath $configPath

if (Test-Path -LiteralPath $resolvedOutput) {
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedOutput | Out-Null

function Copy-RelativeFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    $normalized = $RelativePath.Replace('/', '\')
    if ([System.IO.Path]::IsPathRooted($normalized) -or $normalized.Split('\') -contains '..') {
        throw "Unsafe relative path: $RelativePath"
    }
    $source = Join-Path $resolvedSource $normalized
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required release file missing: $source"
    }
    $destination = Join-Path $resolvedOutput $normalized
    $destinationDirectory = Split-Path -Parent $destination
    if (-not (Test-Path -LiteralPath $destinationDirectory -PathType Container)) {
        New-Item -ItemType Directory -Path $destinationDirectory | Out-Null
    }
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

function Copy-JsonDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$RelativeDirectory,
        [bool]$Required,
        [bool]$Recurse
    )

    $sourceDirectory = Join-Path $resolvedSource $RelativeDirectory
    $destinationDirectory = Join-Path $resolvedOutput $RelativeDirectory
    if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) {
        if ($Required) {
            throw "Required release directory missing: $sourceDirectory"
        }
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        return
    }
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    $files = if ($Recurse) {
        Get-ChildItem -LiteralPath $sourceDirectory -Filter '*.json' -File -Recurse
    } else {
        Get-ChildItem -LiteralPath $sourceDirectory -Filter '*.json' -File
    }
    foreach ($file in $files) {
        $relative = Get-RelativePath -BasePath $resolvedSource -Path $file.FullName
        Copy-RelativeFile -RelativePath $relative
    }
}

$fixedFiles = @(
    'config.toml',
    'scripts/launch/gamepad_start.bat',
    'scripts/launch/gamepad_native_cpp_start.bat',
    'native/vision_native/build/Release/cod_native_runtime.exe',
    'native/vision_native/build/Release/nvinfer_10.dll',
    'native/vision_native/build/Release/nvinfer_plugin_10.dll',
    'native/vision_native/build/Release/SDL2.dll',
    'native/vision_native/build/Release/ViGEmClient.dll'
)
foreach ($relativePath in $fixedFiles) {
    Copy-RelativeFile -RelativePath $relativePath
}

$modelSource = Join-Path $resolvedSource $modelRelative
if (-not (Test-Path -LiteralPath $modelSource -PathType Leaf)) {
    throw "Configured TensorRT engine missing: $modelSource"
}
$modelDirectory = Split-Path -Parent $modelSource
$modelBaseName = [System.IO.Path]::GetFileNameWithoutExtension($modelSource)
$modelCompanions = Get-ChildItem -LiteralPath $modelDirectory -File | Where-Object {
    $_.Name -eq "$modelBaseName.engine" -or
    $_.Name -eq "$modelBaseName.runtime.json" -or
    $_.Name -eq "$modelBaseName.metadata.json"
}
foreach ($file in $modelCompanions) {
    $relative = Get-RelativePath -BasePath $resolvedSource -Path $file.FullName
    Copy-RelativeFile -RelativePath $relative
}
if (-not (Test-Path -LiteralPath (Join-Path $resolvedOutput $modelRelative) -PathType Leaf)) {
    throw 'Configured engine was not copied.'
}

Copy-JsonDirectory -RelativeDirectory $profileRelative -Required $true -Recurse $false
Copy-JsonDirectory -RelativeDirectory $calibrationRelative -Required $false -Recurse $true
Copy-JsonDirectory -RelativeDirectory $weaponRelative -Required $true -Recurse $true
Copy-RelativeFile -RelativePath $stateRelative

$titleUnderline = '=' * $ReleaseTitle.Length
$rollbackLine = if ([string]::IsNullOrWhiteSpace($FallbackRollbackTag)) {
    ''
} else {
    "Fallback rollback tag: $FallbackRollbackTag`r`n"
}
$readme = @"
$ReleaseTitle
$titleUnderline

Source commit: $SourceCommit
$rollbackLine

Start:
  scripts\launch\gamepad_start.bat

Direct smoke command:
  native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --once

This package includes the selected TensorRT engine, native runtime DLLs, launchers,
and referenced recoil profiles/state. It does not require an <engine>.runtime.json
companion for this engine.

Machine-level requirements that are not copied into this folder:
  - Windows display capture/DXGI support
  - NVIDIA driver compatible with CUDA 13.1 and TensorRT 10.15
  - ViGEmBus driver for virtual gamepad output

$($ReleasePurpose.Trim())
"@
Set-Content -LiteralPath (Join-Path $resolvedOutput 'README.txt') -Value $readme -Encoding UTF8

$manifestFiles = @(
    Get-ChildItem -LiteralPath $resolvedOutput -File -Recurse |
        Where-Object { $_.Name -ne 'manifest.json' } |
        Sort-Object FullName |
        ForEach-Object {
            [ordered]@{
                path = Normalize-RelativePath -Path (Get-RelativePath -BasePath $resolvedOutput -Path $_.FullName)
                size = $_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        }
)

$manifest = [ordered]@{
    schema_version = 1
    source_commit = $SourceCommit.ToLowerInvariant()
    created_at_utc = [DateTime]::UtcNow.ToString('o')
    entrypoint = 'scripts/launch/gamepad_start.bat'
    config = 'config.toml'
    model_path = Normalize-RelativePath -Path $modelRelative
    effective_config = [ordered]@{
        profile = Read-TomlScalar -Content $configText -Key 'profile'
        width = Read-TomlScalar -Content $configText -Key 'width'
        height = Read-TomlScalar -Content $configText -Key 'height'
        fps = Read-TomlScalar -Content $configText -Key 'fps'
        tracker_backend = Read-TomlScalar -Content $configText -Key 'tracker_backend'
    }
    files = $manifestFiles
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $resolvedOutput 'manifest.json') -Encoding UTF8

Write-Output "[NativeReleasePackage] root=$resolvedOutput files=$($manifestFiles.Count) commit=$SourceCommit"
