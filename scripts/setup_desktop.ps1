$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pythonCommand = if (Test-Path -LiteralPath 'D:\env\python\python.exe') { 'D:\env\python\python.exe' } else { 'python' }
& $pythonCommand -m pip install -r (Join-Path $projectRoot 'python\desktop_app\requirements.txt')
if ($LASTEXITCODE -ne 0) { throw '桌面依赖安装失败' }
Push-Location (Join-Path $projectRoot 'python\desktop_app\web_ui')
try {
    & npm.cmd ci
    if ($LASTEXITCODE -ne 0) { throw '界面依赖安装失败' }
    & npm.cmd run build
    if ($LASTEXITCODE -ne 0) { throw '界面构建失败' }
} finally { Pop-Location }
Write-Host '桌面界面已构建，可使用根目录的“启动助手.vbs”打开。需要 Windows Edge WebView2 Runtime。'
