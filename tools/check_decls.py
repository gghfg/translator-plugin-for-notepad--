#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
机械比对头文件声明与 .cpp 实际使用，捞出"编译期才会暴露"的低级错误。

针对三类问题（这些是手写代码最容易犯、又只有编译器才会报的）：
  A. .cpp 里用了某个 m_xxx 成员，但头文件里根本没声明它
  B. .cpp 里定义了 ClassName::method，但头文件里没有对应的声明
     （函数名拼错、参数列表不匹配、忘了在头里声明）
  C. 头文件里声明了 ClassName::method，.cpp 里却没有定义
     （会变成链接错误 LNK2019）

本工程是 1 个 .h 配 1 个 .cpp 的结构，所以按同名配对检查即可。
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

# 头文件里声明、但在 .cpp 里不需要再定义的（内联函数 / 纯虚 / 宏等）忽略项
DEFINITION_IGNORE = {
    "config",  # SettingsDialog::config() 是内联的？——实际有定义，留着不影响
}

MEMBER_RE = re.compile(r"\bm_[A-Za-z0-9_]+\b")
# ClassName::methodName(
METHOD_DEF_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_~][A-Za-z0-9_]*)\s*\(")
# 头文件里的函数声明：以 ) 或 ) const 结尾然后 ;（可能带 override / = 0）
METHOD_DECL_RE = re.compile(
    r"^\s*(?:virtual\s+|static\s+|explicit\s+|inline\s+)*"
    r"(?:[A-Za-z_][A-Za-z0-9_:<>,*\s&]*?\s+)?"
    r"([A-Za-z_~][A-Za-z0-9_]*)\s*\([^;{]*\)\s*"
    r"(?:const\s*)?(?:override\s*)?(?:=\s*0\s*)?;",
    re.MULTILINE,
)

# Q_OBJECT 宏、Qt 宏不算方法
KEYWORD_BLACKLIST = {"if", "for", "while", "switch", "return", "Q_UNUSED", "sizeof", "connect"}


def read(path):
    # 必须 utf-8-sig：工程里有带 BOM 的源文件
    return open(path, encoding="utf-8-sig", errors="replace").read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def declared_members(header_text):
    return set(MEMBER_RE.findall(strip_comments(header_text)))


def members_declared_in_cpp(cpp_text):
    """
    收集 .cpp 内部定义的类/结构体里声明的 m_* 成员。

    这类成员不需要在头文件里出现（典型例子：.cpp 匿名命名空间里的 RAII 小类），
    不排除掉就会产生"A 类"误报。
    """
    names = set()
    body = strip_comments(cpp_text)

    for m in re.finditer(r"\b(?:class|struct)\s+[A-Za-z_]\w*[^;{]*\{", body):
        start = m.end() - 1
        depth = 0
        i = start
        while i < len(body):
            if body[i] == "{":
                depth += 1
            elif body[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        names |= set(MEMBER_RE.findall(body[start:i + 1]))

    return names


def declared_methods(header_text):
    """从类体里提取被声明的方法名。"""
    names = set()
    body = strip_comments(header_text)
    for m in METHOD_DECL_RE.finditer(body):
        name = m.group(1)
        if name in KEYWORD_BLACKLIST:
            continue
        names.add(name)
    return names


def defined_methods(cpp_text):
    """返回 {(class, method), ...}"""
    out = set()
    for cls, method in METHOD_DEF_RE.findall(strip_comments(cpp_text)):
        out.add((cls, method))
    return out


def main():
    pairs = []
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith(".cpp"):
            continue
        base = fn[:-4]
        header = os.path.join(SRC, base + ".h")
        if os.path.isfile(header):
            pairs.append((header, os.path.join(SRC, fn)))

    if not pairs:
        print("没找到 .h/.cpp 配对")
        return 2

    problems = 0

    for header, cpp in pairs:
        htext = read(header)
        ctext = read(cpp)

        h_members = declared_members(htext) | members_declared_in_cpp(ctext)
        c_members = set(MEMBER_RE.findall(strip_comments(ctext)))
        h_methods = declared_methods(htext)
        c_defs = defined_methods(ctext)

        rel_h = os.path.relpath(header, ROOT)
        rel_c = os.path.relpath(cpp, ROOT)
        print(f"--- {rel_h}  <->  {rel_c}")

        # A. 用了但没声明的成员
        undeclared = sorted(c_members - h_members)
        if undeclared:
            problems += len(undeclared)
            for name in undeclared:
                print(f"    [A] .cpp 用到成员 {name}，但头文件里没有声明")
        else:
            print(f"    [A] 成员使用 OK（头里声明 {len(h_members)} 个）")

        # 头文件里声明的类名（用于过滤 B 里别的类的定义）
        class_names = set(re.findall(r"\bclass\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::|\{)", htext))

        # B. 定义了但头里没声明的方法
        missing_decl = []
        for cls, method in sorted(c_defs):
            if cls not in class_names:
                continue
            if method.startswith("~") or method in h_methods:
                continue
            if method in DEFINITION_IGNORE:
                continue
            missing_decl.append(f"{cls}::{method}")

        if missing_decl:
            problems += len(missing_decl)
            for item in missing_decl:
                print(f"    [B] .cpp 定义了 {item}，但头文件里找不到声明")
        else:
            print(f"    [B] 方法定义 OK（.cpp 定义 {len(c_defs)} 个）")

        # C. 头里声明了但 .cpp 没定义
        #
        # 这里要排除三种"看起来没定义、其实没问题"的情况，否则全是误报：
        #   1) 头文件里就地内联实现的函数；
        #   2) Qt 信号（signals: 下声明，由 moc 生成实现）；
        #   3) 命名空间里的自由函数——.cpp 里写的是 `inspect(...)` 而不是
        #      `NddHost::inspect(...)`，所以按 "Class::method" 匹配不到。
        # 统一的兜底判据：只要这个方法名在 .cpp 里出现过 "名字(" 就算有实现/有引用。
        defined_names = {m for _c, m in c_defs}
        inline_in_header = set(
            re.findall(r"([A-Za-z_~][A-Za-z0-9_]*)\s*\([^;{]*\)\s*(?:const\s*)?\{",
                       strip_comments(htext))
        )
        ctext_nocomment = strip_comments(ctext)
        mentioned = set(re.findall(r"\b([A-Za-z_~][A-Za-z0-9_]*)\s*\(", ctext_nocomment))

        undefined = sorted(h_methods - defined_names - inline_in_header - mentioned)
        if undefined:
            print(f"    [C] 头里声明但 .cpp 里既无定义也无引用: {', '.join(undefined)}")
        else:
            print(f"    [C] 声明/定义配对 OK")

    print()
    if problems:
        print(f"❌ 发现 {problems} 个疑似编译错误（A/B 类）")
        return 1

    print("✅ 未发现 A/B 类问题：成员使用与 .cpp 方法定义都能在头文件里找到对应声明")
    return 0


if __name__ == "__main__":
    sys.exit(main())
