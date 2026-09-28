#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
校验 OpenSSL 运行库能否给 Qt 5.15 用。

背景（本机踩过的真实坑）
------------------------
Qt 5.15 在 Windows 上只支持 OpenSSL 后端，且写死查找这两个文件名：
    libssl-1_1-x64.dll / libcrypto-1_1-x64.dll
（OpenSSL 3.x 叫 libssl-3-x64.dll，Qt 5.15 不认。）

但**光有正确的文件名和架构还不够**。从某些软件目录里"提取"出来的 OpenSSL DLL
会内嵌 manifest，声明依赖那个软件私有的 CRT 并排程序集，例如：

    <assemblyIdentity name='Avast.VC140.CRT' version='14.0.23918.0'
                      publicKeyToken='fcc99ee6193ebbca' />

这类程序集没装，LoadLibrary 就会直接失败并返回
**Win32 错误 14001 (ERROR_SXS_CANT_GEN_ACTCTX)**，
Qt 于是只能报一句没头没脑的 "TLS initialization failed"。

本脚本把这几件事一次性查清楚：
  1. 文件名与所在目录对不对；
  2. 是不是 64 位（PE machine = 0x8664）；
  3. OpenSSL 版本串（必须是 1.1.x）；
  4. 内嵌 manifest 里有没有"非微软"的并排程序集依赖（有就是废的）；
  5. 用 ctypes 真的 LoadLibrary 一次，直接看能不能加载。

用法：
    python tools/check_openssl.py                        # 默认查 ndd 安装目录
    python tools/check_openssl.py --dir <某个目录>
"""

import argparse
import ctypes
import os
import re
import struct
import sys

DEFAULT_DIR = r"C:\Users\Bob\software\Notepad--v3.9.0-win10-portable"
NEEDED = ("libssl-1_1-x64.dll", "libcrypto-1_1-x64.dll")


def pe_machine(path):
    """读 PE 头里的 machine 字段：0x8664 = x64，0x14c = x86。"""
    try:
        with open(path, "rb") as fh:
            data = fh.read(0x400)
    except OSError:
        return None

    if len(data) < 0x40 or data[:2] != b"MZ":
        return None
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew + 6 > len(data):
        return None
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return None
    return struct.unpack_from("<H", data, e_lfanew + 4)[0]


def openssl_version(path):
    data = open(path, "rb").read()
    for enc in ("ascii", "utf-16-le"):
        m = re.search(rb"OpenSSL [0-9]+\.[0-9]+\.[0-9]+[a-z]?[^\x00]{0,24}"
                      if enc == "ascii" else
                      ("OpenSSL [0-9]+\\.[0-9]+\\.[0-9]+[a-z]?[^\\x00]{0,24}").encode("utf-16-le"),
                      data)
        if m:
            raw = m.group(0)
            return raw.decode(enc, "ignore").strip()
    return None


def manifest_dependencies(path):
    """返回内嵌 manifest 里 dependentAssembly 的程序集名（只留非微软的）。"""
    data = open(path, "rb").read()
    names = set()
    for enc in ("utf-8", "utf-16-le"):
        text = data.decode(enc, "ignore")
        for m in re.finditer(r"name='([^']+)'\s+version='([^']+)'", text):
            names.add((m.group(1), m.group(2)))
        for m in re.finditer(r'name="([^"]+)"\s+version="([^"]+)"', text):
            names.add((m.group(1), m.group(2)))

    # 只关心并排程序集依赖：微软自家的是正常的，其它前缀都说明是"某软件私有"的
    suspicious = []
    for name, version in sorted(names):
        if name.startswith("Microsoft.") or name.startswith("Microsoft.VC"):
            continue
        if re.match(r"^[A-Za-z0-9_.]+\.(CRT|MFC|OpenMP)$", name):
            suspicious.append((name, version))
    return suspicious


def try_load(path):
    if os.name != "nt":
        return None
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.LoadLibraryW.restype = ctypes.c_void_p
    k32.LoadLibraryW.argtypes = [ctypes.c_wchar_p]
    ctypes.set_last_error(0)
    handle = k32.LoadLibraryW(path)
    if handle:
        return True, 0
    return False, ctypes.get_last_error()


def main():
    parser = argparse.ArgumentParser(description="校验 OpenSSL 1.1 运行库是否可用于 Qt 5.15")
    parser.add_argument("--dir", default=DEFAULT_DIR, help="存放这两个 DLL 的目录")
    args = parser.parse_args()

    print(f"检查目录：{args.dir}")
    print()

    all_ok = True
    for name in NEEDED:
        path = os.path.join(args.dir, name)
        print(f"===== {name}")

        if not os.path.isfile(path):
            print("  ❌ 文件不存在\n")
            all_ok = False
            continue

        size = os.path.getsize(path)
        machine = pe_machine(path)
        arch = {0x8664: "x64 ✓", 0x14C: "x86 ❌（必须是 64 位）"}.get(machine, f"未知(0x{machine:x})" if machine else "读不出")

        print(f"  大小     : {size:,} 字节")
        print(f"  架构     : {arch}")
        print(f"  版本串   : {openssl_version(path) or '（没搜到）'}")

        bad = manifest_dependencies(path)
        if bad:
            print("  ❌ manifest 里依赖了非微软的并排程序集（这会让 LoadLibrary 直接失败）：")
            for n, v in bad:
                print(f"        {n}  version={v}")
            print("     典型症状：Win32 错误 14001 (ERROR_SXS_CANT_GEN_ACTCTX)，")
            print("     而 Qt 只会报一句 'TLS initialization failed'。")
            all_ok = False
        else:
            print("  manifest : 没有可疑的并排程序集依赖 ✓")

        ok, err = try_load(path)
        if ok:
            print("  实际加载 : OK ✓")
        else:
            print(f"  实际加载 : ❌ 失败，Win32 错误码 {err}")
            if err == 14001:
                print("             = ERROR_SXS_CANT_GEN_ACTCTX（并排配置不正确）")
            all_ok = False
        print()

    print("=" * 60)
    if all_ok:
        print("结论：这两个 DLL 看起来可用。重启 ndd 后看「诊断…」里的 HTTPS 后端一行。")
        return 0

    print("结论：这两个 DLL 不能给 Qt 5.15 用。")
    print()
    print("请换一个来源重新获取 OpenSSL **1.1.x 64 位** 运行库：")
    print("  · 优先用官方安装包（例如 Win64 OpenSSL v1.1.1w），而不是从别的软件目录里")
    print("    \"提取\"出来的 DLL —— 后者常常带着那个软件私有的 CRT 依赖。")
    print("  · 下好之后先跑一遍本脚本，确认架构、版本、manifest、实际加载四项全过，")
    print("    再放进 ndd 安装目录并重启 ndd。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
