// This module defines various things common to all of the Scintilla Qt port.
//
// Copyright (c) 2023 Riverbank Computing Limited <info@riverbankcomputing.com>
// 
// This file is part of QScintilla.
// 
// This file may be used under the terms of the GNU General Public License
// version 3.0 as published by the Free Software Foundation and appearing in
// the file LICENSE included in the packaging of this file.  Please review the
// following information to ensure the GNU General Public License version 3.0
// requirements will be met: http://www.gnu.org/copyleft/gpl.html.
// 
// If you do not wish to use this file under the terms of the GPL version 3.0
// then you may purchase a commercial license.  For more information contact
// info@riverbankcomputing.com.
// 
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.


#ifndef QSCIGLOBAL_H
#define QSCIGLOBAL_H

#include <qglobal.h>


#define QSCINTILLA_VERSION      0x020d01
#define QSCINTILLA_VERSION_STR  "2.13.1"


// We only support Qt v5.11 and later.
#if QT_VERSION < 0x050b00
#error "Qt v5.11.0 or later is required"
#endif


// Define QSCINTILLA_MAKE_DLL to create a QScintilla shared library, or
// define QSCINTILLA_DLL to link against a QScintilla shared library, or define
// neither to either build or link against a static QScintilla library.
#ifdef QSCINTILLA_DLL
#undef QSCINTILLA_DLL
#endif

// 插件是 QScintilla 的"消费者"（不是构建者），必须让 QSCINTILLA_EXPORT 展开成
// __declspec(dllimport)。否则 QsciScintilla::staticMetaObject 会被当成"本模块内的
// 普通数据符号"引用：链接期不报错（.def 导入库同时提供了普通名），但运行期拿到的是
// .idata 导入槽地址而不是真对象，Qt 的 PMF connect/disconnect 会立刻崩。
// 注意：文件里上面的 #ifdef QSCINTILLA_DLL / #undef 会吃掉命令行 -DQSCINTILLA_DLL，
// 所以只能在这里改。
#define QSCINTILLA_DLL

#if defined(QSCINTILLA_DLL)
#define QSCINTILLA_EXPORT       Q_DECL_IMPORT
#elif defined(QSCINTILLA_MAKE_DLL)
#define QSCINTILLA_EXPORT       Q_DECL_EXPORT
#else
#define QSCINTILLA_EXPORT
#endif


#if !defined(QT_BEGIN_NAMESPACE)
#define QT_BEGIN_NAMESPACE
#define QT_END_NAMESPACE
#endif

#endif
