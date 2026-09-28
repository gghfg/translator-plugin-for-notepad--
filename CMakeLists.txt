cmake_minimum_required(VERSION 3.16)

project(ndd-deepseek-translate LANGUAGES CXX)

# ---------------------------------------------------------------------------
# notepad-- (ndd) DeepSeek 翻译插件
#
# 目标环境（必须与宿主一致，否则 ABI 不兼容会导致崩溃）：
#   · Windows x64
#   · MSVC（ndd 官方包是 MSVC 构建，绝不能用 MinGW）
#   · Qt 5.15.2（官方包内 Qt5Core.dll 就是 5.15.2）
#
# 强烈建议只构建 Release：Debug 会引入 /MDd 与 _ITERATOR_DEBUG_LEVEL=2，
# 与宿主的 Release 运行库不匹配，跨 DLL 传递 QString/std::function 时可能出问题。
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

if(MSVC)
	# 强制使用动态 CRT（/MD）。宿主 qmyedit_qt5.dll 是 /MD 构建的，
	# 两边 CRT 必须一致，否则跨 DLL 释放内存会崩。
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

	# /utf-8 是必需的：源码里有大量中文字符串字面量，
	# 不加这个 MSVC 会按本地代码页解释源文件，界面全是乱码。
	add_compile_options(/utf-8 /permissive- /W3)

	add_compile_definitions(UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

	# Qt 5.15.2 与新 MSVC STL 的兼容垫片（stdext 命名空间已被 STL 删除）。
	# 详见 src/compat/qt_stdext_shim.h 顶部的说明。
	# 注意：这里用 /FI 强制包含，因此对 moc 生成的文件同样生效。
	add_compile_options("/FI${CMAKE_CURRENT_SOURCE_DIR}/src/compat/qt_stdext_shim.h")
endif()

# Concurrent：WinHTTP 是同步接口，请求要放到线程池里跑，不能卡住界面。
# 不再需要 Network：传输层已从 Qt 的 QNetworkAccessManager 换成 WinHTTP。
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

# ---------------------------------------------------------------------------
# 头文件来源：二选一
#   A) 已 clone notepad-- 源码   -> 设 -DNDD_SOURCE_DIR=<仓库路径>
#   B) 用 third_party/include 里随工程附带的头文件（默认）
# ---------------------------------------------------------------------------
set(NDD_SDK_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "插件 SDK 目录（含 include 与 lib）")
set(NDD_SOURCE_DIR "" CACHE PATH "notepad-- 源码目录（可选，设了就用仓库里的头文件）")
set(NDD_PLUGIN_DIR "" CACHE PATH "ndd 的 plugin 目录（可选，设了就在构建后自动拷贝 DLL）")

set(NDD_INCLUDE_DIRS "")

if(NDD_SOURCE_DIR)
	if(NOT EXISTS "${NDD_SOURCE_DIR}/src/qscint/src/Qsci/qsciscintilla.h")
		message(FATAL_ERROR
			"NDD_SOURCE_DIR 指向的目录里找不到 src/qscint/src/Qsci/qsciscintilla.h：\n"
			"  ${NDD_SOURCE_DIR}\n"
			"请确认指向 notepad-- 仓库根目录（也就是包含 src/ 的那一层）。")
	endif()

	list(APPEND NDD_INCLUDE_DIRS
		"${NDD_SOURCE_DIR}/src"
		"${NDD_SOURCE_DIR}/src/include"
		"${NDD_SOURCE_DIR}/src/qscint/src"
		"${NDD_SOURCE_DIR}/src/qscint/src/Qsci"
		"${NDD_SOURCE_DIR}/src/qscint/scintilla/include"
	)

	message(STATUS "头文件来源：notepad-- 源码 ${NDD_SOURCE_DIR}")
else()
	list(APPEND NDD_INCLUDE_DIRS
		"${NDD_SDK_DIR}/include"
		"${NDD_SDK_DIR}/include/Qsci"        # 供 #include <qsciscintilla.h>
		"${NDD_SDK_DIR}/include/scintilla"   # 供 Scintilla.h / SciLexer.h
	)

	if(NOT EXISTS "${NDD_SDK_DIR}/include/Qsci/qsciscintilla.h")
		message(FATAL_ERROR
			"third_party/include 下缺少 QScintilla 头文件。\n"
			"请二选一：\n"
			"  1) 从 notepad-- 仓库复制 src/qscint/src/Qsci/*.h 到 third_party/include/Qsci/，\n"
			"     再复制 src/qscint/scintilla/include/*.h 到 third_party/include/scintilla/；\n"
			"  2) 或者 clone 仓库后用 -DNDD_SOURCE_DIR=<仓库路径> 直接引用源码里的头文件。")
	endif()

	message(STATUS "头文件来源：内置 SDK ${NDD_SDK_DIR}/include")
endif()

# ---------------------------------------------------------------------------
# 配置期护栏：必须让 QSCINTILLA_EXPORT 展开成 __declspec(dllimport)
#
# 漏掉这一点的后果很隐蔽：编译通过、链接通过，但运行期 Qt 的 PMF
# connect/disconnect 会因为拿到假的 QsciScintilla::staticMetaObject 而立刻崩溃。
# 详见 third_party/include/Qsci/qsciglobal.h 里的注释。
# ---------------------------------------------------------------------------
set(_qsci_global_h "")
foreach(_dir IN LISTS NDD_INCLUDE_DIRS)
	if(EXISTS "${_dir}/Qsci/qsciglobal.h")
		set(_qsci_global_h "${_dir}/Qsci/qsciglobal.h")
		break()
	endif()
endforeach()

if(_qsci_global_h)
	file(READ "${_qsci_global_h}" _qsci_global_content)
	# 只认"没被注释掉"的 #define QSCINTILLA_DLL
	if(NOT _qsci_global_content MATCHES "(^|\n)[ \t]*#[ \t]*define[ \t]+QSCINTILLA_DLL")
		message(FATAL_ERROR
			"${_qsci_global_h} 没有打开 QSCINTILLA_DLL 开关。\n"
			"这将导致 QsciScintilla 不是 dllimport，QsciScintilla::staticMetaObject\n"
			"会在运行期解析到导入槽而不是真对象，插件一加载（挂接 selectionChanged 时）就崩。\n"
			"修法：把该文件里被注释掉的 `//#define QSCINTILLA_DLL` 打开。\n"
			"（命令行 -DQSCINTILLA_DLL 无效，文件里 40-42 行有 #undef 会把它吃掉。）")
	endif()
	message(STATUS "QSCINTILLA_DLL 开关：已打开 ✓")
else()
	message(WARNING "没找到 Qsci/qsciglobal.h，跳过 QSCINTILLA_DLL 检查")
endif()

# ---------------------------------------------------------------------------
# 导入库：qmyedit_qt5.lib
# ndd 安装包只给运行期 DLL，不给 .lib。用 tools/gen_import_lib.ps1 从
# 自带的 qmyedit_qt5.dll 导出符号表生成 .lib（无需重编 QScintilla 源码）。
# ---------------------------------------------------------------------------
set(NDD_QMYEDIT_LIB "${NDD_SDK_DIR}/lib/qmyedit_qt5.lib" CACHE FILEPATH "qmyedit_qt5 导入库路径")
set(NDD_QMYEDIT_DEF "${NDD_SDK_DIR}/lib/qmyedit_qt5.def" CACHE FILEPATH "qmyedit_qt5 导出符号表（.def）")

# 仓库里只提交 .def（纯文本，125KB），不提交 .lib（867KB 二进制产物）。
# 配置时用同一套工具链里的 lib.exe 自动把 .def 变成导入库，做到 clone 即可编译。
# CMAKE_LINKER 指向 link.exe，lib.exe 就在它旁边，这样不用依赖 PATH 里有 VS 环境。
if(NOT EXISTS "${NDD_QMYEDIT_LIB}" AND EXISTS "${NDD_QMYEDIT_DEF}")
	get_filename_component(_ndd_linker_dir "${CMAKE_LINKER}" DIRECTORY)
	find_program(NDD_LIB_EXE NAMES lib.exe HINTS "${_ndd_linker_dir}" NO_DEFAULT_PATH)

	if(NOT NDD_LIB_EXE)
		find_program(NDD_LIB_EXE NAMES lib.exe)
	endif()

	if(NDD_LIB_EXE)
		file(MAKE_DIRECTORY "${NDD_SDK_DIR}/lib")
		message(STATUS "导入库不存在，正在从 .def 生成……")
		execute_process(
			COMMAND "${NDD_LIB_EXE}" /nologo
			        "/def:${NDD_QMYEDIT_DEF}" /machine:x64 "/out:${NDD_QMYEDIT_LIB}"
			RESULT_VARIABLE _ndd_lib_result
			OUTPUT_VARIABLE _ndd_lib_out
			ERROR_VARIABLE  _ndd_lib_err)
		if(NOT _ndd_lib_result EQUAL 0)
			message(FATAL_ERROR
				"从 .def 生成导入库失败（lib.exe 返回 ${_ndd_lib_result}）：\n"
				"${_ndd_lib_out}\n${_ndd_lib_err}")
		endif()
		message(STATUS "导入库已生成 ✓")
	else()
		message(WARNING "系统里找不到 lib.exe，无法自动生成导入库")
	endif()
endif()

if(NOT EXISTS "${NDD_QMYEDIT_LIB}")
	message(FATAL_ERROR
		"缺少导入库：${NDD_QMYEDIT_LIB}\n"
		"正常情况下配置阶段会自动从 ${NDD_QMYEDIT_DEF} 生成。\n"
		"如果自动生成没成功，可以手动运行：\n"
		"  powershell -ExecutionPolicy Bypass -File tools/gen_import_lib.ps1 `\n"
		"      -Dll \"<ndd安装目录>\\qmyedit_qt5.dll\" -OutDir third_party\\lib")
endif()

# ---------------------------------------------------------------------------
# 目标
# ---------------------------------------------------------------------------
set(PLUGIN_SOURCES
	src/pluginentry.cpp
	src/selectionassistant.cpp
	src/translationpopup.cpp
	src/settingsdialog.cpp
	src/deepseekclient.cpp
	src/winhttpclient.cpp
	src/translatorconfig.cpp
	src/nddhost.cpp
)

set(PLUGIN_HEADERS
	src/selectionassistant.h
	src/translationpopup.h
	src/settingsdialog.h
	src/deepseekclient.h
	src/winhttpclient.h
	src/translatorconfig.h
	src/nddhost.h
)

add_library(ndd-deepseek-translate SHARED ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})

target_include_directories(ndd-deepseek-translate PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	${NDD_INCLUDE_DIRS}
)

target_link_libraries(ndd-deepseek-translate PRIVATE
	Qt5::Core
	Qt5::Gui
	Qt5::Widgets
	Qt5::Concurrent
	"${NDD_QMYEDIT_LIB}"
)

if(WIN32)
	# translatorconfig.cpp 用 CryptProtectData 加密保存 API Key
	target_link_libraries(ndd-deepseek-translate PRIVATE crypt32)
	# winhttpclient.cpp 用 WinHTTP + Schannel 发 HTTPS，不再依赖 OpenSSL
	target_link_libraries(ndd-deepseek-translate PRIVATE winhttp)
endif()

set_target_properties(ndd-deepseek-translate PROPERTIES
	PREFIX ""
	OUTPUT_NAME "ndd-deepseek-translate"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
)

# VS 这类多配置生成器会自动给 RUNTIME_OUTPUT_DIRECTORY 追加 $<CONFIG>，
# 产物会落到 plugin/Release/ 而不是文档里写的 plugin/。
# 这里按配置逐个指定，让文档中的路径真的成立。
foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
	set_target_properties(ndd-deepseek-translate PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/plugin"
		LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/plugin")
endforeach()

if(NDD_PLUGIN_DIR)
	add_custom_command(TARGET ndd-deepseek-translate POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different
			"$<TARGET_FILE:ndd-deepseek-translate>"
			"${NDD_PLUGIN_DIR}/"
		COMMENT "拷贝插件到 ndd 的 plugin 目录"
	)
endif()

# ---------------------------------------------------------------------------
# 可选：DeepSeekClient 的本地联调测试
#
# 插件真机验证需要 API Key + 鼠标点击，而网络层（请求组装 / 鉴权头 / 响应解析 /
# HTTP 状态码映射）完全是可独立测试的。打开这个开关会额外构建一个小程序，
# 配合 tools/mock_deepseek_server.py 就能在没有 Key 的情况下跑通整条链路。
# 默认关闭，不影响插件本身。
# ---------------------------------------------------------------------------
option(NDD_BUILD_TESTS "构建本地联调测试程序（对着 mock 服务器跑）" OFF)

if(NDD_BUILD_TESTS)
	add_executable(ndd-translate-client-test
		tests/deepseekclient_smoke.cpp
		src/deepseekclient.cpp
		src/deepseekclient.h
		src/winhttpclient.cpp
		src/winhttpclient.h
		src/translatorconfig.cpp
		src/translatorconfig.h
	)

	target_include_directories(ndd-translate-client-test PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		${NDD_INCLUDE_DIRS}
	)

	# 刻意只链 Core + Concurrent：这个测试不碰 QScintilla，也不需要 Widgets
	target_link_libraries(ndd-translate-client-test PRIVATE Qt5::Core Qt5::Concurrent)

	if(WIN32)
		target_link_libraries(ndd-translate-client-test PRIVATE crypt32 winhttp)
	endif()

	set_target_properties(ndd-translate-client-test PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin")

	foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
		set_target_properties(ndd-translate-client-test PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/plugin")
	endforeach()

	message(STATUS "已启用联调测试目标：ndd-translate-client-test")

	# -----------------------------------------------------------------------
	# 端到端测试（默认关闭，需要单独打开）
	#
	# 它要 new 一个真实的 QsciScintilla，因此只有**头文件与 DLL 完全同一修订**
	# 才能链接成功——只要缺任何一个虚函数的定义就会 LNK2001。
	#
	# 本工程自带的 QScintilla 头文件来自 GitHub 镜像，与 ndd v3.9.0 的
	# qmyedit_qt5.dll 并非同一修订（实测：双向比对后净差 5 个虚函数槽位）。
	# 所以这个目标默认关闭，以免默认构建失败。
	#
	# 插件本身**不需要**构造 QsciScintilla，也不依赖它的 vtable 布局
	# （见 src/nddhost.cpp 里 replaceSelectedText 的限定名调用说明），
	# 因此头文件版本漂移不影响插件功能。
	#
	# 等你拿到与 DLL 同版本的头文件后：
	#     cmake -S . -B build -DNDD_BUILD_TESTS=ON -DNDD_BUILD_E2E_TEST=ON ...
	# 即可构建并运行。
	# -----------------------------------------------------------------------
	option(NDD_BUILD_E2E_TEST "构建端到端测试（要求 QScintilla 头文件与 DLL 同版本）" OFF)

	if(NDD_BUILD_E2E_TEST)
	add_executable(ndd-translate-e2e-test
		tests/selectionassistant_e2e.cpp
		src/selectionassistant.cpp
		src/selectionassistant.h
		src/translationpopup.cpp
		src/translationpopup.h
		src/settingsdialog.cpp
		src/settingsdialog.h
		src/deepseekclient.cpp
		src/deepseekclient.h
		src/winhttpclient.cpp
		src/winhttpclient.h
		src/translatorconfig.cpp
		src/translatorconfig.h
		src/nddhost.cpp
		src/nddhost.h
	)

	target_include_directories(ndd-translate-e2e-test PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		${NDD_INCLUDE_DIRS}
	)

	# 需要 QsciScintilla，所以要链 qmyedit_qt5
	target_link_libraries(ndd-translate-e2e-test PRIVATE
		Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Concurrent
		"${NDD_QMYEDIT_LIB}"
	)

	if(WIN32)
		target_link_libraries(ndd-translate-e2e-test PRIVATE crypt32 winhttp)
	endif()

	set_target_properties(ndd-translate-e2e-test PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin")

	foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
		set_target_properties(ndd-translate-e2e-test PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/plugin")
	endforeach()

	message(STATUS "已启用端到端测试目标：ndd-translate-e2e-test")
	endif()
endif()

message(STATUS "")
message(STATUS "ndd-deepseek-translate 配置完成")
message(STATUS "  构建类型   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  输出       : ${CMAKE_BINARY_DIR}/plugin/ndd-deepseek-translate.dll")
message(STATUS "  导入库     : ${NDD_QMYEDIT_LIB}")
if(NDD_PLUGIN_DIR)
	message(STATUS "  自动部署到 : ${NDD_PLUGIN_DIR}")
else()
	message(STATUS "  提示       : 可用 -DNDD_PLUGIN_DIR=<ndd 的 plugin 目录> 让构建后自动拷贝")
endif()
message(STATUS "")
