$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path
$markerPath="$root/runs/desktop/long-capture.json"
if(Test-Path $markerPath){
    $info=Get-Content $markerPath -Raw|ConvertFrom-Json
    $allowed=[IO.Path]::GetFullPath("$root/output/")
    $target=[IO.Path]::GetFullPath($info.directory)
    if(!$target.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid capture directory'}
    [IO.File]::WriteAllText((Join-Path $target 'stop'),'stop')
    $info.enabled=$false;$info|ConvertTo-Json|Set-Content -Encoding utf8 $markerPath
    Write-Output 'System collectors stopping. Stop the assistant normally in the GUI to drain its remaining logs.'
}
