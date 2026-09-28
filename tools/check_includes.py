#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
验证 third_party/include 里的头文件 include 闭包是否完整。

做法：扫描所有 .h 里的 #include，凡是能对应到工程内头文件的（Qsci/、scintilla/），
就按 CMakeLists 里配置的 include 目录去解析；解析不到的就是缺失的头文件。

CMakeLists 中配置的 include 目录：
    third_party/include
    third_party/include/Qsci        (供 #include <qsciscintilla.h>)
    third_party/include/scintilla   (供 Scintilla.h / SciLexer.h)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INCLUDE_ROOT = os.path.join(ROOT, "third_party", "include")
SRC_ROOT = os.path.join(ROOT, "src")

INCLUDE_DIRS = [
    INCLUDE_ROOT,
    os.path.join(INCLUDE_ROOT, "Qsci"),
    os.path.join(INCLUDE_ROOT, "scintilla"),
    SRC_ROOT,
]

# 这些前缀的头文件由 Qt / 系统 / MSVC 提供，不在我们管辖范围
EXTERNAL_PREFIXES = (
    "Qt", "Q", "q", "ui_", "windows.h", "wincrypt.h", "tchar.h",
    "std", "string", "vector", "functional", "memory", "algorithm",
    "c", "sys/", "unistd.h",
)

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)


def is_external(name):
    """粗判是否外部头文件：带扩展名且不在工程目录里的常见库头。"""
    base = os.path.basename(name)
    if base.startswith("Q") or base.startswith("q"):
        # Qt 的头是大驼峰 Q 开头，或小写 q 开头；但工程里有 qsciscintilla.h 这类
        if base.startswith("Qsci"):
            return False
        return True
    return False


def is_project_header(name):
    """
    判断某个 include 是否属于"本工程/ndd SDK 应当提供的头文件"。

    只有这类头解析不到才算真问题。Qt 的 <QString>、CRT 的 <functional>/<stddef.h>
    等外部头不归我们管，解析不到是正常的，不能报成错误。
    """
    base = os.path.basename(name)
    return (
        name.startswith("Qsci/")
        or name.startswith("scintilla/")
        or base.startswith("qsc")          # qsciscintilla.h / qscilexer.h ...
        or base == "pluginGl.h"
    )


def should_check(quote, name):
    """
    某个 include 是否值得校验。

    校验范围：
      1) ndd SDK / QScintilla 的头（qsc*.h、Qsci/、scintilla/、pluginGl.h）——即使我们没有，
         也必须报出来，因为那意味着工程缺文件；
      2) 所有用引号写的 include —— 本工程约定：引号只用于自己的头文件，Qt 一律用尖括号。
         这样能抓到 include 名拼错、文件漏加之类的问题。
    """
    if is_project_header(name):
        return True
    if quote == '"':
        return True
    return False


def collect_files():
    files = []
    for base in (INCLUDE_ROOT, SRC_ROOT):
        for dirpath, _dirnames, filenames in os.walk(base):
            for fn in filenames:
                # 头文件和实现文件都要扫：include 写错在哪边都可能
                if fn.endswith((".h", ".hpp", ".cpp", ".cc", ".cxx")):
                    files.append(os.path.join(dirpath, fn))
    return files


def resolve(name, current_file):
    """按编译器规则解析 include：先当前目录，再依次是各个 include 目录。"""
    candidates = []

    if os.path.isabs(name) or ".." in name:
        return None

    # 1) 相对于当前文件所在目录（仅对 "..." 形式，这里放宽一点也无妨）
    candidates.append(os.path.join(os.path.dirname(current_file), name))

    # 2) 各个 -I 目录
    for d in INCLUDE_DIRS:
        candidates.append(os.path.join(d, name))

    for c in candidates:
        if os.path.isfile(c):
            return os.path.normpath(c)

    return None


def main():
    files = collect_files()
    if not files:
        print("找不到任何头文件，请先补齐 third_party/include")
        return 2

    print(f"扫描 {len(files)} 个头文件\n")

    # 工程内存在的头文件集合（用于判断某个 unresolved 到底是不是工程头）
    known = set()
    for f in files:
        known.add(os.path.basename(f))

    missing = {}
    project_includes = set()

    for f in files:
        try:
            # 必须用 utf-8-sig：上游头文件带 UTF-8 BOM，
            # 若用 utf-8 读，BOM(U+FEFF) 会留在首行开头，导致首行的 #include 匹配不到，
            # 检查器就会"空通过"。这个坑是反证测试抓出来的。
            text = open(f, encoding="utf-8-sig", errors="replace").read()
        except OSError as exc:
            print(f"  读取失败 {f}: {exc}")
            continue

        for quote, name in INCLUDE_RE.findall(text):
            if not should_check(quote, name):
                continue

            project_includes.add(name)

            if resolve(name, f) is None:
                missing.setdefault(name, []).append(os.path.relpath(f, ROOT))

    print(f"被校验的 include 共 {len(project_includes)} 个不同头文件")

    if not project_includes:
        print("⚠️  一个都没校验到，说明过滤逻辑有问题，请检查 should_check()")
        return 2

    if not missing:
        print("✅ include 闭包完整：所有被校验的 include 都能解析到实际文件。")
        return 0

    print(f"❌ 有 {len(missing)} 个 include 解析不到：\n")
    for name in sorted(missing):
        print(f"  缺失: {name}")
        for src in sorted(set(missing[name]))[:6]:
            print(f"        被 {src} 引用")
    return 1


if __name__ == "__main__":
    sys.exit(main())
