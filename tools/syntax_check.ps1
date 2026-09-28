#!/usr/bin/env pwsh
# ---------------------------------------------------------------------------
# 对"Qt 依赖已被 tools\syntax-probe 里的桩覆盖"的源文件做真实 MSVC 语法检查。
#
# 为什么要这个：开发这台机器没装 Qt，无法做真正的端到端编译。
# 这个脚本用一组最小 Qt 桩 + cl.exe /Zs（只做语法检查）把源文件喂给真编译器，
# 能抓出：语法错误、标识符拼错、信号名写错、用了未声明的成员、类型不匹配。
#
# ⚠️ 它证明不了"Qt API 用对了"——桩的签名是按本工程用法写的。
#    另外 QObject::connect/disconnect 用的是"照单全收"的可变参数模板，
#    所以**不校验信号槽参数是否匹配**。
#
# 用法： pwsh -File tools\syntax_check.ps1
# ---------------------------------------------------------------------------

$ErrorActionPreference = 'Continue'

$root  = Split-Path -Parent $PSScriptRoot
$run   = Join-Path $PSScriptRoot 'syntax-probe\run.bat'
$srcDir = Join-Path $root 'src'

# 只有桩覆盖到的文件才能在这里检查。
# 新增源文件时，若它 include 了桩里没有的 Qt 类，先往 syntax-probe 里补桩。
$files = @(
	'translatorconfig.cpp'
	'deepseekclient.cpp'
	'nddhost.cpp'
	'translationpopup.cpp'
	'selectionassistant.cpp'
	'settingsdialog.cpp'
	'pluginentry.cpp'
)

if (-not (Test-Path $run)) {
	Write-Host "找不到 $run" -ForegroundColor Red
	exit 2
}

Write-Host "MSVC 语法检查（/Zs，基于最小 Qt 桩）" -ForegroundColor Cyan
Write-Host ""

$failed = @()

foreach ($f in $files) {
	$path = Join-Path $srcDir $f
	if (-not (Test-Path $path)) {
		Write-Host ("  MISS  {0}" -f $f) -ForegroundColor Yellow
		$failed += $f
		continue
	}

	$out = & cmd /c "`"$run`" `"$path`"" 2>&1
	if ($LASTEXITCODE -eq 0) {
		Write-Host ("  OK    {0}" -f $f) -ForegroundColor Green
	}
	else {
		Write-Host ("  FAIL  {0}" -f $f) -ForegroundColor Red
		$out | Select-Object -Skip 1 | ForEach-Object { Write-Host "        $_" }
		$failed += $f
	}
}

Write-Host ""
if ($failed.Count -eq 0) {
	Write-Host ("全部 {0} 个文件通过语法检查。" -f $files.Count) -ForegroundColor Green
	exit 0
}

Write-Host ("{0} / {1} 个文件未通过：{2}" -f $failed.Count, $files.Count, ($failed -join ', ')) -ForegroundColor Red
exit 1
