<#
.SYNOPSIS
    从 notepad-- 自带的 qmyedit_qt5.dll 生成 MSVC 导入库 qmyedit_qt5.lib。

.DESCRIPTION
    notepad-- 安装包只提供运行期 DLL，不提供 .lib 导入库，而编译插件必须链接
    QsciScintilla。好在 qmyedit_qt5.dll 导出了全部符号（含 C++ 修饰名），
    因此可以用 dumpbin 导出符号表 -> 生成 .def -> 用 lib.exe 造出 .lib。

.PARAMETER Dll
    qmyedit_qt5.dll 的完整路径。

.PARAMETER OutDir
    输出目录（.def 与 .lib 都写在这里）。

.PARAMETER MsvcBin
    可选，包含 dumpbin.exe / lib.exe 的目录。不填则自动从 vswhere 探测。

.EXAMPLE
    pwsh -File gen_import_lib.ps1 `
        -Dll "C:\Users\Bob\software\Notepad--v3.9.0-win10-portable\qmyedit_qt5.dll" `
        -OutDir ".\third_party\lib"
#>
param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$MsvcBin
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Dll)) { throw "找不到 DLL: $Dll" }
$Dll = (Resolve-Path -LiteralPath $Dll).Path

# ---- 定位 MSVC 工具 ---------------------------------------------------------
function Find-MsvcBin {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }

    $installs = & $vswhere -all -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null

    foreach ($inst in $installs) {
        $toolsRoot = Join-Path $inst 'VC\Tools\MSVC'
        if (-not (Test-Path $toolsRoot)) { continue }

        # 取版本号最高的 toolset
        $toolset = Get-ChildItem $toolsRoot -Directory |
            Sort-Object { [version]($_.Name -replace '[^0-9.]', '') } -Descending |
            Select-Object -First 1
        if (-not $toolset) { continue }

        $bin = Join-Path $toolset.FullName 'bin\Hostx64\x64'
        if (Test-Path (Join-Path $bin 'dumpbin.exe')) { return $bin }
    }
    return $null
}

if (-not $MsvcBin) { $MsvcBin = Find-MsvcBin }
if (-not $MsvcBin) { throw "未找到 MSVC 工具集，请安装 VS Build Tools 的 C++ 组件，或用 -MsvcBin 指定目录。" }

$dumpbin = Join-Path $MsvcBin 'dumpbin.exe'
$libexe  = Join-Path $MsvcBin 'lib.exe'
foreach ($t in @($dumpbin, $libexe)) {
    if (-not (Test-Path $t)) { throw "缺少工具: $t" }
}

Write-Host "MSVC 工具目录 : $MsvcBin"
Write-Host "输入 DLL      : $Dll"

# ---- 导出符号表 -------------------------------------------------------------
Write-Host "`n[1/3] dumpbin /exports ..."
$raw = & $dumpbin /nologo /exports $Dll
if ($LASTEXITCODE -ne 0) { throw "dumpbin 失败，退出码 $LASTEXITCODE" }

# 形如:  75    4A 001B9E50 ??0QsciScintilla@@QEAA@PEAVQWidget@@@Z
$names = New-Object System.Collections.Generic.List[string]
foreach ($line in $raw) {
    if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)\s*$') {
        $names.Add($Matches[1])
    }
}

$names = $names | Sort-Object -Unique
if ($names.Count -eq 0) { throw "未能从 DLL 解析出任何导出符号。" }

$qsci = ($names | Where-Object { $_ -like '*QsciScintilla*' }).Count
Write-Host ("      导出符号 {0} 个，其中 QsciScintilla 相关 {1} 个" -f $names.Count, $qsci)

# ---- 生成 .def --------------------------------------------------------------
$base = [System.IO.Path]::GetFileNameWithoutExtension($Dll)
if (-not (Test-Path -LiteralPath $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$OutDir  = (Resolve-Path -LiteralPath $OutDir).Path
$defPath = Join-Path $OutDir "$base.def"
$libPath = Join-Path $OutDir "$base.lib"

Write-Host "[2/3] 写出 $defPath ..."
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("LIBRARY $base")
[void]$sb.AppendLine("EXPORTS")
foreach ($n in $names) { [void]$sb.AppendLine("    $n") }
# .def 必须用 ASCII/ANSI，且换行用 CRLF
[System.IO.File]::WriteAllText($defPath, $sb.ToString(), [System.Text.Encoding]::ASCII)

# ---- 生成 .lib --------------------------------------------------------------
Write-Host "[3/3] lib /def ..."
& $libexe /nologo /def:"$defPath" /machine:x64 /out:"$libPath"
if ($LASTEXITCODE -ne 0) { throw "lib.exe 失败，退出码 $LASTEXITCODE" }

if (-not (Test-Path -LiteralPath $libPath)) { throw "导入库未生成。" }

$size = (Get-Item -LiteralPath $libPath).Length
Write-Host ("`n完成: {0} ({1:N0} 字节)" -f $libPath, $size)
Write-Host "提示: 把该 .lib 放到工程的 third_party/lib 下，并在 CMake 里链接。"
