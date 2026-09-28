#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
校验「工程自带的 QScintilla 头文件」与「宿主 qmyedit_qt5.dll」的 ABI 是否一致。

为什么需要它
------------
插件与宿主之间是靠 QScintilla 的类布局对齐的。如果头文件和 DLL 不是同一修订，
**编译能过、链接也能过，但虚函数调用会跳到错误的函数上**——因为虚函数的槽位下标
是编译期按头文件里虚函数的声明顺序算出来的。

这个坑在本项目上真实发生过：ndd v3.9.0 的 qmyedit_qt5.dll 与公开仓库里的
src/qscint 源码**不是同一份**——DLL 有 ndd 私有的虚函数
（`changeOpenWithQuickMode`、`updateLineNumberWidth` 等），又完全没有
`findFirst` / `findFirstInSelection` / `findNext` / `replace`。gitee 上
master + 全部 53 个 tag 的 `qsciscintilla.h` 都是同一个 blob，没有任何一个
公开版本能对上。所以插件必须写成**不依赖 vtable 布局**的形式（见 README）。

原理
----
MSVC 的 C++ 修饰名里编码了函数的 cv/虚属性：
    QEAA = public 非虚        QEBA = public 非虚 const
    UEAA = public 虚          UEBA = public 虚 const
    MEAA = protected 虚       MEBA = protected 虚 const
而**函数名就是第一个 `@` 之前的那一段**。所以只要把 DLL 导出表里
`\?名字@类名@@[UM]E[AB]A` 的「名字」抽出来，就得到了 DLL 侧真实的虚成员集合；
再和头文件里 `virtual ... 名字(` 的集合做**双向差集**，就能看出两边差在哪。

用法
----
    python tools/check_qsci_abi.py                       # 自动找 dumpbin 并跑
    python tools/check_qsci_abi.py --exports exp.txt      # 用已有的 dumpbin 输出
    python tools/check_qsci_abi.py --dll <路径> --include <目录>

退出码：0 = 两边一致；1 = 存在失配；2 = 无法完成检查（工具/文件缺失）
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_INCLUDE = os.path.join(ROOT, "third_party", "include")
DEFAULT_DLL = r"C:\Users\Bob\software\Notepad--v3.9.0-win10-portable\qmyedit_qt5.dll"

CLASSES = ("QsciScintilla", "QsciScintillaBase")

# moc 生成 / 编译器生成的，任何一份头文件都不会手写它们，不该算作失配
IGNORED_NAMES = {
    "qt_metacall",
    "qt_metacast",
    "metaObject",
    "tr",
    "trUtf8",
    "qt_static_metacall",
}

SYMBOL_RE = re.compile(r"\?([A-Za-z_~]\w*)@(?:%s)@@[UM]E[AB]A" % "|".join(CLASSES))
VIRTUAL_DECL_RE = re.compile(
    r"virtual\s+[A-Za-z_][\w:<>,\s\*&]*?\s+([A-Za-z_~]\w*)\s*\(")


def find_dumpbin():
    """优先用 PATH 上的，其次从 vswhere 找最高版本 toolset。"""
    found = shutil.which("dumpbin")
    if found:
        return found

    vswhere = os.path.join(
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
        "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        return None

    try:
        out = subprocess.run(
            [vswhere, "-all", "-prerelease", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            capture_output=True, text=True, timeout=60).stdout
    except Exception:
        return None

    best = None
    for inst in [line.strip() for line in out.splitlines() if line.strip()]:
        tools = os.path.join(inst, "VC", "Tools", "MSVC")
        if not os.path.isdir(tools):
            continue
        for ver in os.listdir(tools):
            cand = os.path.join(tools, ver, "bin", "Hostx64", "x64", "dumpbin.exe")
            if os.path.isfile(cand) and (best is None or ver > best[0]):
                best = (ver, cand)
    return best[1] if best else None


def get_exports_text(args):
    if args.exports:
        with open(args.exports, encoding="utf-8-sig", errors="replace") as fh:
            return fh.read()

    if not os.path.isfile(args.dll):
        print(f"找不到 DLL：{args.dll}")
        return None

    dumpbin = find_dumpbin()
    if not dumpbin:
        print("找不到 dumpbin.exe（需要 VS 的 C++ 工具集）")
        return None

    print(f"dumpbin: {dumpbin}")
    print(f"分析 DLL: {args.dll}")
    res = subprocess.run([dumpbin, "/nologo", "/exports", args.dll],
                         capture_output=True, text=True, errors="replace")
    if res.returncode != 0:
        print("dumpbin 执行失败：")
        print(res.stdout[-2000:])
        return None
    return res.stdout


def collect_header_virtuals(include_dir):
    qsci_dir = os.path.join(include_dir, "Qsci")
    if not os.path.isdir(qsci_dir):
        print(f"找不到 {qsci_dir}")
        return None

    names = set()
    files = sorted(glob.glob(os.path.join(qsci_dir, "*.h")))
    for path in files:
        with open(path, encoding="utf-8-sig", errors="replace") as fh:
            text = fh.read()
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        names |= set(VIRTUAL_DECL_RE.findall(text))

    return names, [os.path.basename(p) for p in files]


def main():
    parser = argparse.ArgumentParser(description="校验 QScintilla 头文件与 DLL 的 ABI 一致性")
    parser.add_argument("--dll", default=DEFAULT_DLL, help="qmyedit_qt5.dll 路径")
    parser.add_argument("--include", default=DEFAULT_INCLUDE, help="插件 SDK include 目录")
    parser.add_argument("--exports", help="用已有的 dumpbin /exports 文本，免去重新跑 dumpbin")
    args = parser.parse_args()

    exports = get_exports_text(args)
    if exports is None:
        return 2

    header = collect_header_virtuals(args.include)
    if header is None:
        return 2
    header_virtuals, header_files = header

    dll_virtuals = set(SYMBOL_RE.findall(exports))

    only_dll = sorted((dll_virtuals - header_virtuals) - IGNORED_NAMES)
    only_hdr = sorted((header_virtuals - dll_virtuals) - IGNORED_NAMES)

    print()
    print(f"头文件（{', '.join(header_files)}）声明的虚函数：{len(header_virtuals)} 个")
    print(f"DLL 里的 {'/'.join(CLASSES)} 虚成员：{len(dll_virtuals)} 个")
    print()

    if only_hdr:
        print(f"❌ 只在头文件里（DLL 没有 → 头文件的 vtable 会多出这些槽位）{len(only_hdr)} 个：")
        for name in only_hdr:
            print(f"     - {name}")
    else:
        print("✅ 只在头文件里：无")

    print()

    if only_dll:
        print(f"❌ 只在 DLL 里（头文件没声明 → 头文件的 vtable 会少这些槽位）{len(only_dll)} 个：")
        for name in only_dll:
            print(f"     + {name}")
    else:
        print("✅ 只在 DLL 里：无")

    print()
    if not only_dll and not only_hdr:
        print("结论：两边虚函数集合一致 —— vtable 布局应当对得上。")
        print("      仍然建议只调用在导出表里逐条核对过签名的方法。")
        return 0

    print(f"结论：存在失配（头文件独有 {len(only_hdr)} / DLL 独有 {len(only_dll)}）——")
    print("      vtable 布局**不一致**，绝不能对 QsciScintilla 做普通的虚函数调用。")
    print()
    print("应对办法（本工程采用的就是这套）：")
    print("  1) 只调用非虚函数，且用导出表逐条核对过修饰名（非虚调用按名字链接，不走 vtable）；")
    print("  2) 万不得已要调虚函数，写成限定名形式 editor->QsciScintilla::foo(...)，")
    print("     这会抑制虚派发、直接引用导入符号；")
    print("  3) 不要 new / 继承 QsciScintilla（那会需要完整 vtable）。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
