<#
把编译好的插件安装到 ndd 的 plugin 目录。

用法：
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\install_to_ndd.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\install_to_ndd.ps1 `
        -NddDir "D:\somewhere\Notepad--v3.9.0-win10-portable" -BuildDir build

注意：ndd 运行时插件 DLL 会被占用，必须先完全关闭 ndd。
#>
param(
	[string]$NddDir   = "C:\Users\Bob\software\Notepad--v3.9.0-win10-portable",
	[string]$BuildDir = "build-verify"
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root "$BuildDir\plugin\ndd-deepseek-translate.dll"
$dst  = Join-Path $NddDir "plugin\ndd-deepseek-translate.dll"

if (-not (Test-Path -LiteralPath $src)) {
	throw "找不到构建产物：$src`n请先按 README 第 3 节完成构建，或用 -BuildDir 指定别的构建目录。"
}

$pluginDir = Split-Path $dst -Parent
if (-not (Test-Path -LiteralPath $pluginDir)) {
	throw "找不到 ndd 的 plugin 目录：$pluginDir`n请用 -NddDir 指定 ndd 安装目录。"
}

$running = @(Get-Process -Name "Notepad--" -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
	Write-Host ("ndd 正在运行（PID " + ($running.Id -join ", ") + "），插件 DLL 被占用，无法覆盖。") -ForegroundColor Yellow
	Write-Host "请先完全关闭 ndd，然后重新运行本脚本。" -ForegroundColor Yellow
	exit 2
}

Copy-Item -LiteralPath $src -Destination $dst -Force
$item = Get-Item -LiteralPath $dst

Write-Host ""
Write-Host ("已安装：" + $item.FullName) -ForegroundColor Green
Write-Host ("        " + ("{0:N0}" -f $item.Length) + " 字节    " + $item.LastWriteTime)
Write-Host ""
Write-Host "接下来：启动 ndd → 菜单「插件 → DeepSeek 翻译 → 诊断…」，"
Write-Host "确认【传输层】一行显示 WinHTTP（不再需要 OpenSSL）。"
