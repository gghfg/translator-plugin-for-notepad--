# ndd DeepSeek 翻译插件

![PixPin_2026-09-29_04-47-03.png](C:\Users\Bob\Desktop\PixPin_2026-09-29_04-47-03.png)



![PixPin_2026-09-29_04-47-13.png](C:\Users\Bob\Desktop\PixPin_2026-09-29_04-47-13.png)



给国产跨平台文本编辑器 **notepad--（ndd）** 写的原生 C++ 插件。

**交互只有一件事**：在编辑器里选中一段文本 → 编辑器角落出现一个「译」小按钮 →
点它 → 在选区旁边浮窗显示 DeepSeek 的译文（Esc 或点别处关闭）。

- 宿主版本：notepad-- v3.9.0（Windows x64）
- 目标 ABI：MSVC x64 + Qt 5.15.2（与官方包内 `Qt5Core.dll` / `qmyedit_qt5.dll` 一致）
- 插件形式：`<ndd安装目录>\plugin\ndd-deepseek-translate.dll`
- 版本：v1.2.0（传输层为 WinHTTP + Schannel）
- 许可：**GPL-3.0**（见 [LICENSE](LICENSE)）

---

## 0. 传输层：WinHTTP（不需要 OpenSSL）

**传输层用的是 Windows 自带的 WinHTTP + Schannel**，不依赖任何外部 DLL，TLS 由系统维护。因此：

- 不需要下载 OpenSSL，也不会遇到"缺 DLL / 架构不对 / 版本不匹配 / manifest 依赖"那一类问题；
- 插件 DLL 的依赖里只有 `WINHTTP.dll`、`Qt5Core/Gui/Widgets`、`qmyedit_qt5.dll`、
  `CRYPT32.dll` 和 MSVC 运行库 —— **已经没有 `Qt5Network.dll`**。

> 这段是踩坑换来的。插件最初走 Qt 的 `QNetworkAccessManager`，而 Qt 5.15 在 Windows
> 桌面上**只支持 OpenSSL 后端**（没有 schannel；`plugins/tls` 目录不存在，
> `Qt5Network.dll` 里搜不到任何 `schannel`/`secur32`），并且**写死**要找
> `libssl-1_1-x64.dll` / `libcrypto-1_1-x64.dll`（OpenSSL 3.x 的 `libssl-3-x64.dll`
> 它根本不看）。
> 
> 实测更坑的是：连"文件名、架构、OpenSSL 版本（1.1.1g）全都对"的两个 DLL
> **仍然加载不了** —— `LoadLibrary` 返回 **Win32 错误 14001
> (`ERROR_SXS_CANT_GEN_ACTCTX`)**，因为它们的 manifest 里写着依赖
> **Avast 私有的 CRT 并排程序集** `Avast.VC140.CRT`（说明这两个 DLL 是从 Avast
> 安装目录里提取出来的）。而 Qt 只会报一句没头没脑的 `TLS initialization failed`。
> 
> 换成 WinHTTP 之后，上面这整类问题一次性消失，也不用再依赖已经 EOL 的 OpenSSL 1.1。
> 
> `tools/check_openssl.py` 仍留在仓库里，但**当前版本用不到它** ——
> 只有在你想退回 Qt 传输层时才需要。

---

## 0. 交付状态

**已实际编译成功、已装入 ndd 并确认被加载。** 具体证据见第 8 节。摘要：

| 项目                                       | 状态                                                                                 |
| ---------------------------------------- | ---------------------------------------------------------------------------------- |
| 真机编译（MSVC 14.51 + Qt 5.15.2 msvc2019_64） | ✅ 退出码 0，**0 error / 0 warning**                                                    |
| 产物                                       | ✅ `build-verify\plugin\ndd-deepseek-translate.dll`，125,440 字节                      |
| 导出符号                                     | ✅ `NDD_PROC_IDENTIFY`、`NDD_PROC_MAIN`（无修饰名）                                        |
| 依赖                                       | ✅ Qt5Widgets/Gui/Core、qmyedit_qt5、**WINHTTP**、CRYPT32（**已无 Qt5Network、无 OpenSSL**） |
| 装入 ndd 并启动                               | ✅ 进程存活，**插件 DLL 确认在进程模块列表中**                                                       |
| 网络层端到端（对着本地 mock 服务器）                    | ✅ 5 个场景全过（正常/取消/401/非JSON/choices空）                                                |
| 与 DLL 的 ABI 风险（vtable 布局）                | ✅ 已识别并用"不依赖 vtable"的写法规避（见第 7 节陷阱三）                                                |
| **鼠标点按钮 → 看到译文**                         | ❌ **未验证**（需要真实 API Key + 人工操作）                                                     |

**头文件与 DLL 并非同一修订**（第 7 节陷阱三有完整证据），这不是能靠换头文件解决的——
插件因此被刻意写成**不依赖 QScintilla 的 vtable 布局**，所以这个失配不影响功能。

插件**现在已经装在**你的 ndd 里了（`plugin\ndd-deepseek-translate.dll`）。
填好 API Key 就能用；不想要了直接删掉这个 DLL 即可。

---

## 1. 为什么插件能这么写（宿主机制）

从 ndd 上游源码 `src/pluginGl.h`、`src/plugin.cpp`、`src/cceditor/ccnotepad.cpp`
以及本机 `Notepad--v3.9.0-win10-portable` 的实际二进制确认：

- 主程序启动时扫描 `<exe目录>\plugin\*.dll`，`resolve("NDD_PROC_IDENTIFY")` 取插件元信息；

- 本插件声明 `m_menuType = 1`（自建二级菜单），主程序随后再 `resolve("NDD_PROC_MAIN")`，
  把已建好的子菜单 `m_rootMenu` 传进来，并附上：
  
  | 参数                                              | 用途             |
  | ----------------------------------------------- | -------------- |
  | `QWidget* pNotepad`                             | 主窗口指针          |
  | `std::function<QsciScintilla*()> getCurEdit`    | 取当前编辑框         |
  | `std::function<bool(int,void*)> pluginCallBack` | 回调主程序功能（本插件未用） |

- 插件 DLL 放进 `plugin` 目录即可，**加载只在启动时发生一次，加完要重启 ndd**。

### 三个必须处理的坑

**1. hex 视图也是 `QsciScintilla`。** 上游 `ScintillaHexEditView : public QsciScintilla`，
而 `getCurEdit()` 返回 `QsciScintilla*`。不过脑子就把 hex 模式的"文本"送去翻译再写回，
二进制文件会被毁掉。本插件读取 ndd 挂在编辑器上的动态属性来识别模式：

| 属性名        | 含义                                         |
| ---------- | ------------------------------------------ |
| `type`     | 文档类型：1=普通文本 2=大文本只读 3=大文本读写 4=超大文本只读 5=hex |
| `code`     | 当前编码 id                                    |
| `filePath` | 当前文档路径                                     |

只有 `type == 1` 且非只读时才显示「译」按钮；否则**连按钮都不出现**。

**2. 没有"标签页切换"回调。** 插件只在菜单被点击时被通知，感知不到用户切了标签页。
所以助手用 `QTimer`（350ms）轮询 `getCurEdit()` 检测编辑器切换，切换后重新挂接
`QsciScintilla::selectionChanged` 信号。按钮做成编辑器的子控件，切标签/关标签时
会自动跟着隐藏或销毁，不需要自己同步可见性。

**3. `NDD_PROC_MAIN` 会被调用不止一次 —— 这是必现的坑。**
上游 `slot_changeChinese()` / `slot_changeEnglish()`（用户切换界面语言时）里写了：

```cpp
if (m_isToolMenuLoaded)
{
    ui.menuPlugin->clear();            // 把整个「插件」菜单清空，我们的子菜单一起被删
    ui.menuPlugin->addAction(ui.actionPlugin_Manager);
    ui.menuTools->clear();
    m_isToolMenuLoaded = false;
    slot_dynamicLoadToolMenu();        // 重新加载 -> 再次调用 NDD_PROC_MAIN
}
```

也就是说用户**一切换界面语言，我们建好的子菜单连同里面所有 `QAction` 都会被删掉**，
然后主程序重新问我们要一次菜单。因此入口把两件事分开：

- **助手只建一次**（它挂在主窗口下，不受菜单清空影响）；
- **菜单每次重建** —— 绝不能写"已加载就直接 `return`"那种保护，否则语言一切菜单就空了。

---

## 2. 目录结构

```
ndd-deepseek-translate/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── pluginentry.cpp        DLL 入口：NDD_PROC_IDENTIFY / NDD_PROC_MAIN、菜单
│   ├── selectionassistant.h/.cpp  ★ 核心：跟踪编辑器、显示/定位「译」按钮、发起翻译
│   ├── translationpopup.h/.cpp    译文浮窗（Qt::Popup，自动关闭）
│   ├── deepseekclient.h/.cpp      DeepSeek 接口异步客户端
│   ├── settingsdialog.h/.cpp      设置对话框
│   ├── translatorconfig.h/.cpp    配置读写 + API Key 用 DPAPI 加密
│   ├── nddhost.h/.cpp             与宿主交互：识别编辑器模式、读选区、算像素位置、写回
│   └── compat/qt_stdext_shim.h    Qt 5.15.2 × 新 MSVC STL 的兼容垫片（见第 7 节陷阱一）
├── tests/
│   ├── deepseekclient_smoke.cpp   网络层联调测试（不需要真实 API Key，已验证）
│   └── selectionassistant_e2e.cpp 端到端测试（默认关闭，见第 6 节）
├── third_party/
│   ├── include/
│   │   ├── pluginGl.h        ndd 插件 SDK 头
│   │   ├── Qsci/             QScintilla 头（gitee v3.9.0 版；qsciglobal.h 已开 QSCINTILLA_DLL）
│   │   └── scintilla/        三个备用头，**编译并不需要**（见第 7 节陷阱一）
│   └── lib/
│       ├── qmyedit_qt5.def   导出符号表（纯文本，2403 个符号）—— 仓库里提交的就是它
│       └── qmyedit_qt5.lib   导入库（867 KB 二进制）—— **不在版本库里**，
│                             配置阶段由 CMake 调用 lib.exe 从上面的 .def 自动生成
├── tools/
│   ├── install_to_ndd.ps1    ★ 把构建好的 DLL 装进 ndd（会先检查 ndd 是否在运行）
│   ├── gen_import_lib.ps1        从 qmyedit_qt5.dll 生成导入库
│   ├── check_qsci_abi.py     ★ 校验头文件与 DLL 的 ABI 是否一致（双向虚函数集合比对）
│   ├── qmyedit_qt5.exports.txt   DLL 的 dumpbin /exports 快照（上面那个工具用它，免重跑）
│   ├── mock_deepseek_server.py   本地 mock 接口（联调用）
│   ├── check_openssl.py          校验 OpenSSL 1.1 能否给 Qt 5.15 用
│   │                             （**当前版本用不到**，只在退回 Qt 传输层时才有意义）
│   ├── check_includes.py         校验 include 闭包完整（带反证测试）
│   └── check_decls.py            校验头文件声明与 .cpp 定义/使用一致（带反证测试）
└── optional/
    └── whole-document-translation/   整篇翻译的段落切分逻辑（**不在构建里**）
```

`optional/` 里是早期做的"整篇文档翻译"：按段落切分、译后按原文空白精确拼回，
带 20088 用例的往返属性测试。当前需求不需要它，所以移出了构建，留着以备将来要用。

---

## 3. 构建

### 3.0 前置条件

| 组件            | 要求                     | 本机现状                          |
| ------------- | ---------------------- | ----------------------------- |
| Visual Studio | 含"使用 C++ 的桌面开发"，x64    | ✅ VS 18 BuildTools，MSVC 14.51 |
| CMake         | ≥ 3.16                 | ✅ 4.3.1（随 VS 提供）              |
| Qt            | **5.15.2 msvc2019_64** | ✅ `C:\Qt\5.15.2\msvc2019_64`  |

> **必须用 MSVC，不能用 MinGW。** 插件与宿主之间会跨 DLL 传递 `std::function`、`QString`
> 等 C++ 对象，编译器/运行库不一致会直接崩。
> 
> **只构建 Release。** Debug 会引入 `/MDd` 和 `_ITERATOR_DEBUG_LEVEL=2`，与宿主的
> Release 运行库不匹配。CMakeLists 已强制 `MultiThreadedDLL`。

### 3.1 导入库：配置阶段自动生成，不用你管

ndd 安装包只给运行期 `qmyedit_qt5.dll`，不给 `.lib`，而编译插件必须链接 `QsciScintilla`。
好在 `qmyedit_qt5.dll` 导出了全部符号（**2403 个，其中 478 个 QsciScintilla 相关，
C++ 修饰名齐全**），可以直接反推导入库，**不需要重编 QScintilla 源码**。

仓库里只提交 `.def`（纯文本符号表，125 KB），**不提交 `.lib`**（867 KB 二进制产物）。
首次配置时 CMake 会自动用同一套工具链里的 `lib.exe` 把 `.def` 变成 `.lib`，
所以 clone 下来直接配置就行，日志里会看到：

```
-- 导入库不存在，正在从 .def 生成……
-- 导入库已生成 ✓
```

需要手动重新生成时（例如换了 ndd 版本、要更新符号表）：

```powershell
powershell -ExecutionPolicy Bypass -File tools\gen_import_lib.ps1 `
    -Dll "C:\Users\Bob\software\Notepad--v3.9.0-win10-portable\qmyedit_qt5.dll" `
    -OutDir third_party\lib
```

### 3.2 头文件

**工程里已经附带好了**：gitee 仓库 v3.9.0（= master）的 5 个 Qsci 头，include 闭包完整。

⚠️ 但要注意：**它们与 ndd v3.9.0 自带的 `qmyedit_qt5.dll` 并非同一修订**
（头文件独有 4 个虚函数，DLL 独有 3 个）。因此插件刻意写成不依赖 vtable 布局的形式，
详见第 7 节陷阱三。改动 `src/` 时请遵守那里的三条规矩，并用
`python tools\check_qsci_abi.py` 复核。

（早期做 A/B 对照用的 GitHub 镜像版头文件没有放进版本库，需要时可按第 7 节陷阱三的办法自行获取。）

若想直接引用仓库源码（不推荐——那样更难保证与 DLL 同版本）：

```powershell
git clone --depth 1 https://gitee.com/cxasm/notepad--.git C:\src\notepad--
# 配置时加： -DNDD_SOURCE_DIR=C:/src/notepad--
```

⚠️ 用仓库源码时**务必确认那份 `Qsci/qsciglobal.h` 也打开了 `QSCINTILLA_DLL`**
（见第 7 节陷阱二）。CMakeLists 里有配置期检查会直接报错拦住你，不会让你编出一个运行时必崩的 DLL。

### 3.3 配置与编译

在 **x64 Native Tools Command Prompt for VS** 里执行（或先跑 `vcvars64.bat`）：

```powershell
cd ndd-deepseek-translate

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
      -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64 `
      -DNDD_PLUGIN_DIR="C:/Users/Bob/software/Notepad--v3.9.0-win10-portable/plugin"

cmake --build build --config Release
```

`-DNDD_PLUGIN_DIR=...` 可选，填了会在构建后自动把 DLL 拷到 ndd 的 `plugin` 目录。
产物：`build\plugin\ndd-deepseek-translate.dll`

其它可用生成器（本机实测）：`Ninja`、`NMake Makefiles`、`Visual Studio 17 2022`。
用 Ninja 记得显式 `-DCMAKE_BUILD_TYPE=Release`。

### 3.4 安装

**先完全关闭 ndd**（插件 DLL 被占用时无法覆盖），然后：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\install_to_ndd.ps1
```

脚本会检查 ndd 是否还在运行、把 DLL 复制到 `<ndd安装目录>\plugin\`，并提示下一步。
也可以手动复制，或者配置时加 `-DNDD_PLUGIN_DIR=...` 让构建后自动拷贝。

装好后**重启 ndd**。菜单栏「插件」下会出现「DeepSeek 翻译」。

---

## 4. 使用

1. 首次使用：菜单「插件 → DeepSeek 翻译 → 设置…」，填入 DeepSeek 的 API Key，
   点「测试连接」确认通了，再「保存」。
2. 在编辑器里**选中一段文本**，编辑器右下角（或选区末尾，可配置）出现「译」按钮。
3. **点「译」**，选区旁浮出译文窗口。窗口里可以：
   - **复制**：把译文放进剪贴板
   - **替换选区**：用译文替换编辑器中选中的文本（可 Ctrl+Z 整体撤销）
   - `Esc` 或点界面别处关闭

菜单项：

| 菜单项     | 说明                                                                                       |
| ------- | ---------------------------------------------------------------------------------------- |
| 翻译选中文本  | 不用点按钮，直接翻译当前选区，快捷键 **Ctrl+Alt+T**                                                        |
| 设置…     | API Key、模型、语言、提示词、按钮位置、超时                                                                |
| 重新载入配置  | 手改了 ini 之后不用重启                                                                           |
| **诊断…** | 列出插件实际看到的状态：宿主绑定、编辑器（类名/type 属性/模式判定/选区长度）、窗口内搜到的所有编辑器、按钮状态、配置、**传输层**。**按钮不出现或报错时先看这个** |
| 关于      | 版本与配置文件路径                                                                                |

### 编辑器是怎么找到的（两道保险）

1. **首选**宿主传进来的 `getCurEdit()` 回调；
2. 它返回 `nullptr` 时（不同 ndd 版本、多窗口、视图类型差异都可能），
   **退化为自己在宿主窗口里搜**：`findChildren<QsciScintilla*>()` 走 Qt 元对象系统，
   跨 DLL 也成立；再按 ndd 挂在控件上的 `type` 属性滤掉 hex / 大文本视图，
   优先取可见的那个（即当前标签页）。

另外 ndd 每开一个新窗口都会再调一次 `NDD_PROC_MAIN`，插件会在那时
**重绑到新窗口**（`rebindHost`），否则会一直盯着第一个窗口，
在别的窗口里就表现为「没有打开的编辑器」、按钮也不出现。

### 几个刻意的设计取舍

- **按钮不抢焦点**（`Qt::NoFocus`）。否则点按钮时编辑器失焦，选区变灰，体验很差。
  按钮是编辑器的子控件，点它不会惊动 Scintilla 的选区，所以我们才能在点击那一刻读到选区内容。
- **hex / 大文本 / 只读模式下不显示按钮**，而不是等用户点了才告知不能用。
- **选区太长直接拒绝**（默认上限 20000 字符，`TranslatorConfig::maxSelectionChars()`）。
  误选中整篇大文件时，宁可提示也不默默发出一个又慢又贵的请求。
- **纯符号/纯数字的选中内容不送翻译**，本地判断即可。
- **「替换选区」前会比对选区是否变过**：翻译期间用户改了选区就拒绝，避免把译文写到错误位置。
- **浮窗用 `Qt::Popup`**：点别处、按 Esc 自动关闭，省掉自己装全局事件过滤器判断"点到外面了"。

---

## 5. 配置

配置文件：`%APPDATA%\notepad\deepseek-translate.ini`
（由 `QSettings(UserScope + IniFormat + 组织名 "notepad")` 决定，和 ndd 自己的 `nddsets.ini` 同目录）

| 键                 | 默认值                        | 说明                                           |
| ----------------- | -------------------------- | -------------------------------------------- |
| `apiKeyProtected` | —                          | API Key，Windows 下用 **DPAPI 加密**（绑定当前用户），不是明文 |
| `apiKeyPlain`     | —                          | 兼容项：也可直接写明文，程序读到会自动改用加密存储                    |
| `baseUrl`         | `https://api.deepseek.com` | 也接受带 `/v1` 的写法                               |
| `model`           | `deepseek-chat`            | 可选 `deepseek-reasoner`（会自动不发送 temperature）   |
| `temperature`     | `1.0`                      | 0~2                                          |
| `sourceLang`      | `自动检测`                     |                                              |
| `targetLang`      | `中文`                       |                                              |
| `systemPrompt`    | 见代码                        | 翻译规则提示词，可自行调优                                |
| `timeoutMs`       | `60000`                    | 5000~300000                                  |
| `buttonPlacement` | `corner`                   | `corner`=编辑器右下角；`selection`=贴着选区末尾           |

**写回文档**走 `beginUndoAction()` + `replaceSelectedText()` + `endUndoAction()`
（底层 `SCI_REPLACESEL`），**一次 Ctrl+Z 可以整体撤销**。
刻意不用 `QsciScintilla::setText()`——那会清空撤销历史、折叠和标记。

---

## 6. 联调测试（不需要真实 API Key）

网络层（请求组装 / 鉴权头 / 响应解析 / HTTP 状态码映射 / 取消语义）可以完全独立测试：

```powershell
# 1) 构建测试程序
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64 -DNDD_BUILD_TESTS=ON
cmake --build build --config Release

# 2) 起 mock 服务器（另开一个窗口）
python tools\mock_deepseek_server.py 18080 build\mock_request.json

# 3) 跑场景（需要 Qt 的 bin 在 PATH 上）
$env:PATH = "C:\Qt\5.15.2\msvc2019_64\bin;$env:PATH"
.\build\plugin\ndd-translate-client-test.exe http://127.0.0.1:18080/ok
.\build\plugin\ndd-translate-client-test.exe http://127.0.0.1:18080/slow cancel
.\build\plugin\ndd-translate-client-test.exe http://127.0.0.1:18080/unauthorized
```

mock 服务器会把收到的请求（含请求头与请求体）原样写到日志文件，可以核对
`Authorization` 和请求体字段。

### ABI 一致性检查（升级 ndd 后必跑）

```powershell
python tools\check_qsci_abi.py            # 自动找 dumpbin；也可 --exports tools\qmyedit_qt5.exports.txt
```

它把 DLL 导出表里的虚成员集合（修饰名前缀 `UEAA`/`UEBA`/`MEAA`/`MEBA` = 虚函数）
与头文件里 `virtual ...` 的声明集合做**双向差集**，判断 vtable 布局是否对得上。
**每次升级 ndd（换 DLL）都该跑一次**——见第 7 节陷阱三。

### 端到端测试（默认关闭）

`tests/selectionassistant_e2e.cpp` 会造一个真实 `QsciScintilla`，断言
「选中 → 编辑器下出现可见的「译」按钮 → `button->click()` → 译文浮窗出现且内容正确」
——除了"人真的用鼠标点了一下"（用 `click()` 走同一个信号路径代替），整条链路都能自动验证。
它还会先备份 `%APPDATA%\notepad\deepseek-translate.ini`、跑完再恢复，不污染真实配置。

**但它默认不构建**：`new QsciScintilla` 需要完整 vtable，只有头文件与 DLL 完全同修订
才能链接。本工程实测两者并非同一修订（头文件独有 4 个虚函数），所以这个目标用
`-DNDD_BUILD_E2E_TEST=ON` 单独打开；等你拿到与 DLL 同版本的头文件后就能跑。

**也别想用"桩函数补上缺失的虚函数"绕过**：`QSCINTILLA_DLL` 打开后
`QSCINTILLA_EXPORT` = `__declspec(dllimport)`，而 C++ 不允许在消费方定义 dllimport 函数
（MSVC 会报 `C2491: definition of dllimport function not allowed`）。
反过来关掉 dllimport 更糟——既回归第 7 节陷阱二的崩溃，又会用错位的布局在本地生成
一份 vtable。所以这个失配无解，只能靠"不依赖 vtable"的写法规避。

---

## 7. 构建时会遇到的两个 Qt/MSVC 兼容问题

这两个坑都**不涉及插件代码**，但都会让构建失败或运行崩溃，工程里已经处理好了。

### 陷阱一：`stdext` 命名空间已被新版 STL 删除（编译不过）

Qt 5.15.2 的 `QtCore/qcompilerdetection.h` 在识别到 MSVC 时写：

```cpp
#define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) stdext::make_checked_array_iterator(x, size_t(N))
#define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x)  stdext::make_unchecked_array_iterator(x)
```

而 MSVC v14.4x 起（VS 2022 17.12+ / VS 18）的 STL **已经把 `stdext` 整个删掉**，
于是只要编译到 `qlist.h` / `qvector.h` / `qvarlengtharray.h` 就报：

```
qlist.h(915): error C3861: "stdext": 找不到标识符
```

**处理方式**：工程自带 `src/compat/qt_stdext_shim.h`，CMake 用 `/FI` 强制包含它，
在一切之前把这两个宏改成 Qt 自己给非 MSVC 编译器用的写法（裸指针 `(x)`）：

```cmake
add_compile_options("/FI${CMAKE_CURRENT_SOURCE_DIR}/src/compat/qt_stdext_shim.h")
```

因为 `qcompilerdetection.h` 带 include guard，之后 Qt 头再包含它不会重新定义，
所以覆盖一直生效。这两个宏只用于"调试期数组越界检查"，改成裸指针与 Qt 在
GCC/Clang 下的行为一致，**对功能没有影响**。
若将来换用还保留 `stdext` 的旧工具链（v142/v143）或升级到已修复的 Qt，删掉那行 `/FI` 即可。

### 陷阱二：`QsciScintilla` 必须按 dllimport 引用（否则运行必崩）

这是**最阴的一个**：编译通过、链接通过，一运行就崩。

`third_party/include/Qsci/qsciglobal.h` 里上游默认把开关注释掉了：

```cpp
#ifdef QSCINTILLA_DLL
#undef QSCINTILLA_DLL        // ← 命令行 -DQSCINTILLA_DLL 会被这里吃掉
#endif
//#define QSCINTILLA_DLL      // ← 真正的开关
```

于是 `QSCINTILLA_EXPORT` 展开为空，`QsciScintilla` 不是 `__declspec(dllimport)`。
而 Qt 5.15 的 PMF `connect` / `disconnect` 会把
`&SignalType::Object::staticMetaObject` 传给 `connectImpl()`，
这就**必然 ODR 引用 `QsciScintilla::staticMetaObject` 这个数据符号**。
对数据符号，`lib /def` 生成的导入库虽然同时提供了普通名（链接因此不报错），
但普通名解析到的是 `.idata` 导入槽，不是真对象 → 运行期 `connectImpl` 拿到假
`QMetaObject` → 插件第一次挂接 `selectionChanged` 就崩。

**处理方式**：把 `qsciglobal.h` 里那行 `#define QSCINTILLA_DLL` 打开（工程里已改好，
并写了注释说明）。CMakeLists 另加了**配置期护栏**：如果实际使用的 `qsciglobal.h`
没打开这个开关，`cmake` 会直接 `FATAL_ERROR` 报错，而不是让你编出一个运行必崩的 DLL。

**怎么验证修好了**（编译后跑这一条）：

```
dumpbin /symbols build\ndd-deepseek-translate.dir\Release\selectionassistant.obj | findstr staticMetaObject
```

必须看到带 `__imp_` 前缀的：

```
__imp_?staticMetaObject@QsciScintilla@@2UQMetaObject@@B (__declspec(dllimport) ...)
```

如果看到不带 `__imp_` 的 `?staticMetaObject@QsciScintilla@@...`，说明没生效。

旁证：宿主 `plugin` 目录里真实插件（如 `ndd108tool.dll`）的导入表中确实有
`?staticMetaObject@QsciScintilla@@2UQMetaObject@@B`，说明它们编译时是 dllimport。

### 陷阱三：头文件与 DLL **不是同一修订** —— 虚函数调用会跳错槽位

这是本项目踩到的最隐蔽的坑：**编译通过、链接通过、运行必崩或行为诡异**，
而且**换头文件也修不好**。

**事实**（用 `python tools\check_qsci_abi.py` 可复现）：

| 头文件来源                  | 只在头文件里                                                    | 只在 DLL 里                                                         |
| ---------------------- | --------------------------------------------------------- | ---------------------------------------------------------------- |
| gitee v3.9.0（**当前使用**） | 4：`findFirst`、`findFirstInSelection`、`findNext`、`replace` | 3：`changeOpenWithQuickMode`、`toMimeData`、`updateLineNumberWidth` |
| GitHub 镜像（原先）          | 同样 4 个                                                    | 5：上面 3 个 + `playUserMacroRecord`、`startAutoWordCompletion`       |

- gitee 上 **master + cmake-dev 两个分支 + 全部 21 个 v3.x tag** 的 `qsciscintilla.h`
  都是同一个 blob，**不存在第二个候选版本**；GitHub 镜像那份更差。
- 证据表明**这个 DLL 不是用公开仓库那份 `src/qscint` 构建的**：DLL 里有 ndd 私有的
  虚函数（`changeOpenWithQuickMode` 等），又**完全没有** `findFirst`/`findNext`/`replace`
  （连非虚版本都没有），但又有 `getLastFindState`（说明与仓库头同代）。
- 另外 `contextUserDefineMenuEvent` 在 DLL 里是 `(QMenu*, QContextMenuEvent*)` **两个参数**，
  两版头文件里都是**一个参数**——签名也不一致。

**为什么编译器/链接器都发现不了**：虚函数的槽位下标是编译期按头文件声明顺序算出来的，
调用处生成的是 `call [vptr + 偏移]`，链接器根本看不到具体函数名；`.def` 导入库又会
为「普通名」也生成入口，所以即使少了符号也能链接成功。

**本工程采用的写法（三条规矩）**：

1. **只调用非虚函数**，且这些函数的修饰名都用 DLL 导出表逐条核对过。非虚调用按名字
   链接，完全不经过 vtable —— 安全。插件实际用到并已核对的：
   `text`、`selectedText`、`isReadOnly`、`beginUndoAction`、`endUndoAction`、
   `SendScintilla`（13 个重载全在）、`selectionChanged`。
2. **万不得已要调虚函数，写成限定名**：
   
   ```cpp
   editor->QsciScintilla::replaceSelectedText(text);   // 而不是 editor->replaceSelectedText(text)
   ```
   
   限定名会抑制虚派发，直接引用导出符号 `?replaceSelectedText@QsciScintilla@@UEAAXAEBVQString@@@Z`。
   已实测编译产物里确实变成直接引用：
   
   ```
   __imp_?replaceSelectedText@QsciScintilla@@UEAAXAEBVQString@@@Z
   ```
3. **不要 `new` 或继承 `QsciScintilla`**（那会需要完整 vtable；这也是端到端测试默认关闭的原因）。
   基类 `QObject` 的虚函数（如 `metaObject()`）不受影响 —— 它们的槽位由 Qt 自己的头决定。

**信号槽为什么没关系**：Qt 的 PMF `connect` 是拿 `&Class::signal` 的函数地址去
`staticMetaObject` 里做 `IndexOfMethod` 匹配的（按地址，不按序号），
只要这个地址解析到 DLL 里真实那个函数就没问题——导入库保证了这一点。

---

## 8. 验证记录（实际跑过的）

### 编译与链接

```
cmake -S . -B build-verify -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
cmake --build build-verify --config Release
→ 退出码 0，error 0，warning 0
→ build-verify\plugin\ndd-deepseek-translate.dll   125,440 字节
```

符号级核对：

| 检查                                        | 结果                                                                                                                          |
| ----------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `dumpbin /exports`                        | `NDD_PROC_IDENTIFY`、`NDD_PROC_MAIN`（无修饰名）✓                                                                                  |
| `dumpbin /dependents`                     | Qt5Widgets / Qt5Gui / Qt5Core / qmyedit_qt5 / **WINHTTP** / CRYPT32 / MSVCP140 / VCRUNTIME140 ✓（**无 Qt5Network、无 OpenSSL**） |
| `dumpbin /symbols selectionassistant.obj` | `__imp_?staticMetaObject@QsciScintilla@@2UQMetaObject@@B` ✓（陷阱二已修）                                                          |
| `dumpbin /symbols nddhost.obj`            | `__imp_?replaceSelectedText@QsciScintilla@@UEAAXAEBVQString@@@Z` ✓（陷阱三：限定名调用确实变成了直接引用导入符号，不再走 vtable）                       |
| `python tools\check_qsci_abi.py`          | 头文件独有 4 / DLL 独有 3 —— 失配已确认，且已按第 7 节三条规矩规避                                                                                  |
| 自研类                                       | `DeepSeekClient` / `TranslationPopup` 的 `staticMetaObject` 是普通名 ✓（它们定义在本 DLL 内，本就该如此）                                       |

### 装入真实 ndd

```
拷贝 DLL 到 Notepad--v3.9.0-win10-portable\plugin\
启动 Notepad--.exe <测试文件>
→ 进程存活 12 秒，主窗口标题 "[文本模式]"
→ 进程模块列表里出现 ndd-deepseek-translate.dll   ← 插件确实被加载
```

这一步同时验证了陷阱二的修复：有编辑器存在时，助手的 350ms 轮询会走到
`attachToEditor()` → `connect(editor, &QsciScintilla::selectionChanged, ...)`，
**正是未修时必崩的那一行**。

### 网络层端到端（对着 mock 服务器，5/5 通过）

| 场景            | 结果                                                                        |
| ------------- | ------------------------------------------------------------------------- |
| 正常返回          | `RESULT_OK\|你好，世界。这是一次冒烟测试。`，退出码 0                                        |
| 取消语义（翻译中再次触发） | 两次 `BUSY\|1→0`，**只产生 1 个结果**，被取消那次无任何信号                                   |
| 401 鉴权失败      | `HTTP 401：Authentication Fails（authentication_error）` + 「API Key 无效或已过期…」 |
| 非 JSON 响应     | `HTTP 200：接口返回的不是合法 JSON：illegal value`                                   |
| `choices` 为空  | `HTTP 200：接口没有返回任何结果（choices 为空）。`                                        |

服务器实际收到的请求也核对过：

```
Authorization : Bearer test-key-should-appear-as-bearer
Content-Type  : application/json
请求体字段     : messages, model, stream, temperature
model/stream/temperature : deepseek-chat / False / 1
messages 角色  : ['system', 'user']
user 内容      : 把下面的文本翻译成中文。\n\nHello, world. This is a smoke test.
```

中文端到端无乱码，说明 `/utf-8` 与 UTF-8 请求体都正常。

### 传输层：WinHTTP + Schannel（实测确认）

```
# 对着纯 HTTP 的本地服务发 https 请求：应当进到 TLS 握手再失败
ndd-translate-client-test.exe https://127.0.0.1:18080/ok
→ RESULT_FAIL|发送请求失败。
  WinHTTP 错误 12175：TLS 握手失败。常见原因：系统时间不对、根证书缺失、或代理在做中间人劫持。
```

说明 Schannel 确实在做 TLS 握手 —— 而不再像以前那样报"没有可用的 OpenSSL"。
再用 `dumpbin /dependents` 核对：插件 DLL 的依赖里**已经没有 `Qt5Network.dll`**，
换成了 `WINHTTP.dll`，Qt 的 OpenSSL 那条链路彻底不在了。

> **真实接口实测**（有可用网络/代理的环境下）：
> 
> ```
> ndd-translate-client-test.exe https://api.deepseek.com
> → RESULT_FAIL|HTTP 401：Authentication Fails, Your api key: ****arer is invalid
>   (request_id: 3e906770-1b22-4c3e-afa8-18ecb1d85ef8)（authentication_error）
>   API Key 无效或已过期，请在“设置”里重新填写。
> ```
> 
> 用一个**故意填错的 Key** 打真实接口，一次就把整条链路验完了：
> DNS → TCP → **TLS 握手（Schannel）** → HTTP → 真实 JSON 错误解析 →
> `error.message` + `error.type` 拼接 → `friendlyHint(401)`。
> 顺带证明了**自动跟随系统代理确实生效**（当时系统代理指向本机 7890）。
> Key 有效时这一步会返回 200 和真正的译文。
> 
> ⚠️ 勘误：这里早期写的是"本机 TLS 被中间设备拦截"，**那个判断是错的**。
> 真正原因是我用的构建沙箱会阻断 Schannel 的 `AcquireCredentialsHandle`
> （报 `SEC_E_NO_CREDENTIALS`，且连接耗时近乎 0——根本没碰到网络）。
> 非沙箱环境下 Schannel 完全正常。

### 编辑器查找的两道保险（新增，实测待确认）

- 首选宿主 `getCurEdit()`；它返回 `nullptr` 时退化为 `findChildren<QsciScintilla*>()`
  在宿主窗口里自己找（按 `type` 属性滤掉 hex / 大文本，优先取可见的那个）。
- 每次 `NDD_PROC_MAIN` 都重绑宿主窗口（多窗口场景）。
- 菜单新增「诊断…」，把宿主绑定、编辑器列表、按钮状态、配置、传输层一次列清。

这两条是**针对"选中文本后按钮不出现 / 报没有打开的编辑器"**加的；编译与加载已验证，
但**尚未在真实使用场景里确认它们修好了那个现象**——需要用户重启 ndd 后用「诊断」确认。

### 其它静态检查（都做过反证测试）

- `python tools\check_includes.py` —— include 闭包完整（13 个 include 全部解析）
- `python tools\check_decls.py` —— 6 对 .h/.cpp 的声明/定义/成员使用一致
- ~~`tools\syntax_check.ps1` 基于假 Qt 头做语法检查~~ —— **已从仓库移除**。
  它是"还没装 Qt 5.15.2"时的兜底手段；后来有了真 Qt，就以真机编译为准了。
  那套假头跟不上代码演进（缺 `QFutureWatcher`、QtConcurrent 等），会报假错，
  而一个会报假错的检查比没有更糟。需要时可以从 git 历史里取回。
- 三个脚本都做过**反证测试**（故意插入错误必须报警），过程中真的抓到过两次"空通过"

### 仍未验证的

- ❌ **用有效 Key 拿到真实译文（HTTP 200）**：真实接口的 401 路径已经验证过了（见上），
  但 200 响应下 `choices[0].message.content` 的解析还没用真 Key 跑过。
  本地 mock 返回的响应体与真实接口格式一致，风险很低。
- ❌ **鼠标点「译」按钮 → 浮窗显示译文** 这一整条 UI 交互链，需要真实 API Key + 人工操作。
  （`tests/selectionassistant_e2e.cpp` 已经把这条链路写成了自动化测试，但它需要
  头文件与 DLL 完全同版本才能链接，所以默认关闭——见第 6 节。）
- ❌ 按钮的实际观感、浮窗的位置与尺寸、划选时的响应速度
- ❌ `deepseek-reasoner` 模型（代码里会跳过 temperature，但没实际调过）
- ❌ 非 UTF-8 文档（GBK 等）下选区提取是否乱码 —— 理论上 `selectedText()` 会按文档编码解码

### 下一步最该做的三件事

1. **确认新版生效**：菜单「插件 → DeepSeek 翻译 → 诊断…」，【传输层】一行应显示
   `WinHTTP + Schannel（Windows 自带，无需 OpenSSL 等外部库）`。
   如果显示的还是一长串 HTTPS 后端的 OpenSSL 说明，说明装的还是旧版 DLL。
2. 在「设置…」里点「测试连接」，通了之后再选一段英文试按钮。
3. 试一下浮窗里的「替换选区」，确认译文写回后 Ctrl+Z 能整体撤销。

> 顺手可以删掉 ndd 目录里的 `libssl-1_1-x64.dll` / `libcrypto-1_1-x64.dll` ——
> 新版插件不再需要它们，而且那两个是从 Avast 里提取的、本来就加载不了。

### 脚本编码注意

`tools\*.ps1` 都是 **UTF-8 with BOM**。这不是可选项——本机只有 Windows PowerShell 5.1
（没有 `pwsh`），它读无 BOM 的 .ps1 会按 GBK 解释，中文注释直接把脚本语法搞坏。
另外默认执行策略禁止跑脚本，所以要用 `-ExecutionPolicy Bypass`。

---

## 9. 排错

| 现象                                 | 原因 / 处理                                                                             |
| ---------------------------------- | ----------------------------------------------------------------------------------- |
| 「插件」菜单里没有「DeepSeek 翻译」             | DLL 没放进 `<exe目录>\plugin\`；或放进去后没重启；或宿主是**便携版/不支持插件的版本**                             |
| 加载时直接崩溃                            | 十有八九是第 7 节陷阱二（`QSCINTILLA_DLL` 没打开）；也可能是 ABI 不匹配（必须 MSVC x64 + Qt 5.15.2 + Release） |
| 编译报 `stdext: 找不到标识符`               | 第 7 节陷阱一；确认 CMakeLists 里那行 `/FI...qt_stdext_shim.h` 还在                              |
| 界面中文乱码                             | 编译时漏了 `/utf-8`；CMakeLists 已默认加上                                                     |
| `cmake` 报 `没有打开 QSCINTILLA_DLL 开关` | 这是护栏在拦你，按提示改 `qsciglobal.h`（尤其用了 `NDD_SOURCE_DIR` 时）                                |
| 选中文本后按钮不出现                         | 正常保护：hex / 大文本 / 只读模式下不显示，或当前不是普通文本视图                                               |
| 按钮位置不对/被遮住                         | 换「设置 → 悬浮按钮 → 出现位置」，或改成"贴着选区末尾"                                                     |
| 链接错误指向 `QsciScintilla::xxx`        | 导入库路径不对。正常情况下配置阶段会自动从 `.def` 生成（见 3.1）；若日志里出现"系统里找不到 lib.exe"，就手动跑一次 3.1 里那个脚本      |
| 提示 `HTTP 401` / `HTTP 402`         | Key 不对 / 账户余额不足，菜单「设置…」里点「测试连接」                                                     |
| 提示"选中的内容太长"                        | 选区超过 20000 字符，只选中要翻译的部分                                                             |

---

## 10. 许可与发布

### 许可

本项目以 **GPL-3.0** 发布，完整条文见 [LICENSE](LICENSE)。

选 GPL-3.0 的原因：notepad-- 本体就是 GPL-3.0，插件在构建期使用 QScintilla
（GPL-3.0）的头文件、运行期链接宿主自带的 `qmyedit_qt5.dll`，用同一许可证最稳妥。

### 打包发布

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make_release.ps1
```

会在 `dist\` 下生成 `ndd-deepseek-translate-v<版本>-win64.zip`，内含：

```
ndd-deepseek-translate-v1.2.0/
├── ndd-deepseek-translate.dll   编译好的插件
├── 安装说明.txt                 给最终用户的安装/配置/排错说明
├── LICENSE                      GPL-3.0 全文
└── README.md                    完整开发文档
```

版本号从 `src/pluginentry.cpp` 里的 `kPluginVersion` 读取，改一处即可，
可以用 `-RepoUrl` 把安装说明里的仓库地址占位符替换掉：

```powershell
... -File tools\make_release.ps1 -RepoUrl https://github.com/<你>/<仓库>
```

### 仓库怎么组织

`.gitignore` 已排除 `dist/` 和 `third_party/lib/*.lib`：

| 位置       | 内容                                        |
| -------- | ----------------------------------------- |
| 仓库       | 源码 + CMake + 文档 + 工具（纯文本，不含二进制产物）         |
| Releases | `ndd-deepseek-translate-v1.2.0-win64.zip` |

> GPL-3.0 要求分发二进制时同时提供对应源码。仓库本身就是源码，
> 在 Release 说明里给出仓库链接即可满足。
