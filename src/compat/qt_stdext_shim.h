// ---------------------------------------------------------------------------
// Qt 5.15.2 × 新版 MSVC STL 的兼容垫片
// ---------------------------------------------------------------------------
//
// 为什么需要这个文件：
//
//   Qt 5.15.2 的 QtCore/qcompilerdetection.h 在识别到 MSVC 时会写：
//
//       #define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) stdext::make_checked_array_iterator(x, size_t(N))
//       #define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x)   stdext::make_unchecked_array_iterator(x)
//
//   而 MSVC v14.4x 起（VS 2022 17.12+ / VS 18）的 STL 已经把 stdext 命名空间
//   整个删除，于是只要编译到 Qt 的容器头就会报：
//       qlist.h(915): error C3861: "stdext": 找不到标识符
//       qvarlengtharray.h(109) / (553) / (575)、qvector.h(960)、qkeysequence_p.h(81) 同理
//
//   这是 Qt 5.15.2 与新 MSVC 工具链之间的兼容问题，跟插件代码无关。
//
// 本垫片怎么绕过：
//
//   CMake 里用 /FI（强制包含）把它塞进每个编译单元的最前面。
//   它先引入 <QtCore/qglobal.h>（其内部会包含 qcompilerdetection.h），
//   再把这两个宏改成 Qt 自己给非 MSVC 编译器用的写法 —— 也就是裸指针 (x)。
//   由于 qcompilerdetection.h 带 include guard，之后任何 Qt 头再包含它都不会
//   重新定义，所以这里的覆盖会一直生效。
//
// 影响范围：
//
//   这两个宏只用于"调试期的数组越界检查"（checked / unchecked array iterator）。
//   改成裸指针与 Qt 在 GCC / Clang 下的行为完全一致，对功能没有影响。
//   如果将来换用还保留 stdext 的旧工具链（如 v142 / v143），或者升级到已修复的
//   Qt 版本，删掉 CMakeLists 里那行 /FI 即可。
// ---------------------------------------------------------------------------

#pragma once

#include <QtCore/qglobal.h> // 会引入 qcompilerdetection.h

#undef QT_MAKE_CHECKED_ARRAY_ITERATOR
#define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) (x)

#undef QT_MAKE_UNCHECKED_ARRAY_ITERATOR
#define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x) (x)
