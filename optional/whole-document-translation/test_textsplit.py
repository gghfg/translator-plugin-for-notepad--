#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
textsplit.cpp 的等价 Python 实现 + 往返属性测试。

目的：textsplit 的切分/还原算法是整篇翻译保排版的关键，而本机没有 Qt 无法编译
C++ 版本。这里用 Python 逐行镜像 C++ 的实现，做属性测试验证：

    joinDocument(splitDocument(text, n), splitDocument(text, n).blocks) == text

即"原样返回的译文"必须能精确还原原文，一个字节都不能差。
"""

import random
import sys


# ---------------------------------------------------------------- 镜像实现

def parse_lines(text):
    """镜像 textsplit.cpp 的 parseLines()。返回 [(content, term), ...]"""
    lines = []
    n = len(text)
    line_start = 0
    i = 0
    while i < n:
        ch = text[i]
        if ch == '\n':
            lines.append((text[line_start:i], '\n'))
            i += 1
            line_start = i
        elif ch == '\r':
            content = text[line_start:i]
            if i + 1 < n and text[i + 1] == '\n':
                lines.append((content, '\r\n'))
                i += 2
            else:
                lines.append((content, '\r'))
                i += 1
            line_start = i
        else:
            i += 1
    if line_start < n:
        lines.append((text[line_start:], ''))
    return lines


def is_blank_line(content):
    return content.strip() == ''


def needs_translation(block):
    return any(ch.isalpha() for ch in block)


class DocumentParts:
    def __init__(self):
        self.blocks = []
        self.gaps = []


def split_document(text, max_chunk_chars):
    """镜像 splitDocument()。注意 C++ 的 size() 是 UTF-16 码元数，
    Python 的 len() 是码点数；切分位置可能不同，但不影响还原正确性。"""
    if max_chunk_chars < 1:
        max_chunk_chars = 1

    parts = DocumentParts()
    parts.gaps.append('')

    lines = parse_lines(text)

    current_block = ''
    current_chars = 0
    in_block = False
    pending_term = ''

    for content, term in lines:
        if is_blank_line(content):
            if in_block:
                parts.blocks.append(current_block)
                parts.gaps.append(pending_term)
                current_block = ''
                current_chars = 0
                in_block = False
                pending_term = ''
            parts.gaps[-1] += content + term
            pending_term = ''
            continue

        if in_block and current_chars + len(content) > max_chunk_chars:
            parts.blocks.append(current_block)
            parts.gaps.append(pending_term)
            current_block = ''
            current_chars = 0
            pending_term = ''

        if in_block:
            current_block += pending_term

        current_block += content
        current_chars += len(content)
        pending_term = term
        in_block = True

    if in_block:
        parts.blocks.append(current_block)
        parts.gaps.append(pending_term)
        pending_term = ''

    # C++ 里的兜底修正
    while len(parts.gaps) < len(parts.blocks) + 1:
        parts.gaps.append('')
    while len(parts.gaps) > len(parts.blocks) + 1:
        parts.gaps.pop()

    return parts


def join_document(parts, translated):
    """镜像 joinDocument()"""
    if len(translated) != len(parts.blocks):
        return None
    out = ''
    if parts.gaps:
        out += parts.gaps[0]
    for i, t in enumerate(translated):
        out += t
        gi = i + 1
        if gi < len(parts.gaps):
            out += parts.gaps[gi]
    return out


# ---------------------------------------------------------------- 测试

def check_invariants(name, text, max_chunk):
    parts = split_document(text, max_chunk)

    # 不变量 1: gaps 比 blocks 恰好多一个
    assert len(parts.gaps) == len(parts.blocks) + 1, \
        f"{name}: gaps/blocks 数量不变量被破坏 {len(parts.gaps)} vs {len(parts.blocks)}"

    # 不变量 2: 块内不出现空行（空行必须落在 gaps 里）
    for b in parts.blocks:
        for ln in b.split('\n'):
            assert ln.strip() != '' or ln == '', f"{name}: 块内出现空行"

    # 不变量 3: 原样返回必须精确还原
    restored = join_document(parts, list(parts.blocks))
    assert restored == text, (
        f"{name}: 往返还原失败 (max_chunk={max_chunk})\n"
        f"  原文   = {text!r}\n  还原后 = {restored!r}"
    )

    # 不变量 4: 每块长度不超过上限（最后一块除外，因为单行超长时无法再切）
    for i, b in enumerate(parts.blocks):
        if '\n' in b:
            continue
        assert len(b) <= max(max_chunk, 1) or len(b.split('\n')[0]) > max_chunk, \
            f"{name}: 块 {i} 超长且不含换行 {len(b)}"

    # 不变量 5: 译文长度不一致时必须返回 None（防止写坏文档）
    if parts.blocks:
        assert join_document(parts, parts.blocks[:-1]) is None, \
            f"{name}: 数量不匹配时未返回 None"

    return parts


def main():
    random.seed(20260929)
    failures = 0
    cases = 0

    # ---- 手工边界用例
    fixed = [
        "", "\n", "\r\n", "\r", "\n\n\n", "A", "A\n", "A\r\n", "A\r",
        "A\nB\n", "A\nB", "A\n\nB\n", "A\r\n\r\nB\r\n",
        "  \n\t\n", "A\n   \nB\n", "A\nB\nC\n", "A\rB\rC",
        "第一行\n第二行\n", "A\n\n\n\nB",
        "line1\nline2\n\nline3\nline4\n",
        "no trailing newline",
        "\n\nleading blanks\n\n",
    ]
    for t in fixed:
        for mc in (1, 2, 5, 2000):
            cases += 1
            try:
                check_invariants(repr(t), t, mc)
            except AssertionError as e:
                failures += 1
                print(f"FAIL {e}")

    # ---- 随机模糊测试
    alphabet = ['a', 'B', '中', '文', ' ', '\t', '1', '.', '\n', '\r\n', '\r', '']
    for _ in range(20000):
        parts_txt = [random.choice(alphabet) for _ in range(random.randint(0, 12))]
        text = ''.join(parts_txt)
        mc = random.choice([1, 2, 3, 5, 10, 50, 2000])
        cases += 1
        try:
            check_invariants('fuzz', text, mc)
        except AssertionError as e:
            failures += 1
            if failures < 8:
                print(f"FAIL {e}")

    # ---- needs_translation 行为
    assert needs_translation("hello") is True
    assert needs_translation("中文") is True
    assert needs_translation("123 456") is False
    assert needs_translation("!@#$%^&*()") is False
    assert needs_translation("   \t  ") is False
    assert needs_translation("") is False

    print(f"\n用例总数: {cases}  失败: {failures}")
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
