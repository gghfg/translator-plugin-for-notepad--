<#
.SYNOPSIS
    打出可直接上传到 GitHub Releases 的发布包。

.DESCRIPTION
    从构建目录取编译好的插件 DLL，连同给最终用户的安装说明、LICENSE 和 README，
    打成一个带顶层目录的 zip：

        dist\ndd-deepseek-translate-v<版本>-win64.zip
          └── ndd-deepseek-translate-v<版本>\
                ├── ndd-deepseek-translate.dll
                ├── 安装说明.txt
                ├── LICENSE
                └── README.md

    版本号从 src\pluginentry.cpp 里的 kPluginVersion 读取，避免两处不一致。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_release.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_release.ps1 `
        -RepoUrl https://github.com/me/ndd-deepseek-translate
#>
param(
	[string]$BuildDir = "build-verify",
	[string]$Config   = "Release",
	[string]$Version  = "",
	[string]$RepoUrl  = ""
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

# ---- 版本号：从源码里读，保证只有一处定义 -----------------------------------
if (-not $Version) {
	$entry = Join-Path $root 'src\pluginentry.cpp'
	if (-not (Test-Path -LiteralPath $entry)) { throw "找不到 $entry" }

	$line = Select-String -LiteralPath $entry -Pattern 'kPluginVersion\s*=\s*"([^"]+)"' |
		Select-Object -First 1
	if (-not $line) { throw "没能从 pluginentry.cpp 里读出 kPluginVersion" }
	$Version = $line.Matches[0].Groups[1].Value
}
if (-not $Version.StartsWith('v')) { $Version = "v$Version" }

# ---- 定位产物 ---------------------------------------------------------------
$dll = Join-Path $root "$BuildDir\plugin\ndd-deepseek-translate.dll"
if (-not (Test-Path -LiteralPath $dll)) {
	throw "找不到构建产物：$dll`n请先构建（见 README 第 3 节），或用 -BuildDir 指定别的目录。"
}

$license = Join-Path $root 'LICENSE'
$readme  = Join-Path $root 'README.md'
$install = Join-Path $root 'packaging\安装说明.txt'
foreach ($f in @($license, $readme, $install)) {
	if (-not (Test-Path -LiteralPath $f)) { throw "缺少打包所需文件：$f" }
}

# ---- 组装 -------------------------------------------------------------------
$pkgName = "ndd-deepseek-translate-$Version-win64"
$distDir = Join-Path $root 'dist'
$stage   = Join-Path $distDir $pkgName
$zipPath = Join-Path $distDir "$pkgName.zip"

if (Test-Path -LiteralPath $stage)   { Remove-Item -LiteralPath $stage -Recurse -Force }
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $dll     -Destination $stage
Copy-Item -LiteralPath $license -Destination $stage
Copy-Item -LiteralPath $readme  -Destination $stage

# 安装说明里有个仓库地址占位符，按需替换
$installText = [System.IO.File]::ReadAllText($install, [System.Text.Encoding]::UTF8)
if ($RepoUrl) {
	$installText = $installText.Replace('<把这里替换成你的 GitHub 仓库地址>', $RepoUrl)
}
$installOut = Join-Path $stage '安装说明.txt'
[System.IO.File]::WriteAllText($installOut, $installText, (New-Object System.Text.UTF8Encoding($true)))

Compress-Archive -Path $stage -DestinationPath $zipPath -CompressionLevel Optimal

# ---- 报告 -------------------------------------------------------------------
$size = (Get-Item -LiteralPath $zipPath).Length
Write-Host ""
Write-Host "发布包已生成：" -ForegroundColor Green
Write-Host ("  " + $zipPath + "   (" + ("{0:N0}" -f $size) + " 字节)")
Write-Host ""
Write-Host "内容："
Get-ChildItem -LiteralPath $stage | ForEach-Object {
	Write-Host ("  {0,10:N0}  {1}" -f $_.Length, $_.Name)
}
Write-Host ("  {0,10:N0}  {1}" -f $size, "(压缩包总计)")
Write-Host ""
Write-Host "下一步：到 GitHub 建 Release，把这个 zip 传上去。"
if (-not $RepoUrl) {
	Write-Host "提示：安装说明里的仓库地址还是占位符，可用 -RepoUrl <地址> 重新打一次。" -ForegroundColor Yellow
}
