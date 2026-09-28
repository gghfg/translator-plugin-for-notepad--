// ndd DeepSeek 翻译插件 —— 入口点。
//
// 主程序加载流程（来自上游 src/plugin.cpp 与 cceditor/ccnotepad.cpp）：
//   CCNotePad 构造函数 -> init_toolsMenu() -> slot_dynamicLoadToolMenu() -> loadPluginLib()
//     -> 在 <exe目录>/plugin/ 下扫描 *.dll，逐个 resolve "NDD_PROC_IDENTIFY"
//     -> 本插件声明 m_menuType = 1（自建二级菜单）时，主程序再 resolve "NDD_PROC_MAIN"
//        并把已建好的子菜单指针 m_rootMenu 传进来。
//
// ⚠️ 关键：NDD_PROC_MAIN 会被调用**不止一次**。
//   上游 slot_changeChinese() / slot_changeEnglish() 里有这么一段：
//
//       if (m_isToolMenuLoaded)
//       {
//           ui.menuPlugin->clear();                  // 把整个「插件」菜单清空
//           ui.menuPlugin->addAction(ui.actionPlugin_Manager);
//           ui.menuTools->clear();
//           m_isToolMenuLoaded = false;
//           slot_dynamicLoadToolMenu();              // 重新加载 -> 再次调用 NDD_PROC_MAIN
//       }
//
//   也就是说用户一切换界面语言，我们上一步建好的子菜单连同里面所有 QAction
//   都会被删掉，然后主程序重新问我们要一次菜单。
//
//   所以这里必须把两件事分开：
//     · 助手对象（悬浮按钮 + 浮窗）—— 只创建一次；
//     · 菜单项 —— 每次调用都要重建，绝不能"已加载就直接 return"，
//       否则语言一切，插件菜单就变空了。

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QWidget>
#include <functional>

#include <pluginGl.h>
#include <qsciscintilla.h>

#include "selectionassistant.h"
#include "translatorconfig.h"

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

// 把本 DLL 永久钉在进程里。
// 主程序用 QLibrary 解析插件，而 QLibrary 析构时会卸载 DLL；我们已经在宿主里
// 创建了悬浮按钮、浮窗和一堆信号连接，DLL 一旦被卸载，这些对象的虚表与代码就没了，
// 之后必然崩溃。这里主动给模块加一个永久引用计数，杜绝这种可能。
static void pinModuleInProcess()
{
	HMODULE module = nullptr;
	::GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
		reinterpret_cast<LPCWSTR>(&pinModuleInProcess),
		&module);
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

NDD_EXPORT bool NDD_PROC_IDENTIFY(NDD_PROC_DATA* pProcData);
NDD_EXPORT int NDD_PROC_MAIN(QWidget* pNotepad,
							 const QString& strFileName,
							 std::function<QsciScintilla*()> getCurEdit,
							 std::function<bool(int, void*)> pluginCallBack,
							 NDD_PROC_DATA* procData);

#ifdef __cplusplus
}
#endif

namespace {

const char* const kPluginVersion = "v1.2.0";

NDD_PROC_DATA     s_procData;
QWidget*          s_mainWindow = nullptr;
SelectionAssistant* s_assistant = nullptr;

// 构建插件菜单。**每次 NDD_PROC_MAIN 都要调用**，因为主程序切换界面语言时会把
// 整个插件菜单 clear() 掉再重新问我们要。
void buildPluginMenu(QMenu* rootMenu)
{
	QAction* translateAction = rootMenu->addAction(QStringLiteral("翻译选中文本"));
	translateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+T")));
	QObject::connect(translateAction, &QAction::triggered, s_assistant, []() {
		if (s_assistant != nullptr)
		{
			s_assistant->translateSelectionNow();
		}
	});

	rootMenu->addSeparator();

	QAction* settingsAction = rootMenu->addAction(QStringLiteral("设置…"));
	QObject::connect(settingsAction, &QAction::triggered, s_assistant, []() {
		if (s_assistant != nullptr)
		{
			s_assistant->openSettings();
		}
	});

	QAction* reloadAction = rootMenu->addAction(QStringLiteral("重新载入配置"));
	QObject::connect(reloadAction, &QAction::triggered, s_assistant, []() {
		if (s_assistant != nullptr)
		{
			s_assistant->reloadConfig();
		}
	});

	// 排查用：把插件实际看到的状态列出来（编辑器、按钮、配置、HTTPS 后端）
	QAction* diagnoseAction = rootMenu->addAction(QStringLiteral("诊断…"));
	QObject::connect(diagnoseAction, &QAction::triggered, s_assistant, []() {
		if (s_assistant != nullptr)
		{
			s_assistant->showDiagnostics();
		}
	});

	rootMenu->addSeparator();

	QAction* aboutAction = rootMenu->addAction(QStringLiteral("关于"));
	QObject::connect(aboutAction, &QAction::triggered, s_assistant, []() {
		QMessageBox::information(
			s_mainWindow,
			QStringLiteral("关于 DeepSeek 翻译"),
			QStringLiteral(
				"ndd DeepSeek 翻译插件 %1\n\n"
				"用法：在编辑器里选中一段文本，编辑器角落会出现一个「译」按钮，\n"
				"点它即可在选区旁边浮窗显示译文（Esc 或点别处关闭）。\n\n"
				"配置文件：%2")
				.arg(QString::fromLatin1(kPluginVersion), TranslatorConfig::filePath()));
	});
}

} // namespace

bool NDD_PROC_IDENTIFY(NDD_PROC_DATA* pProcData)
{
	if (pProcData == nullptr)
	{
		return false;
	}

	pProcData->m_strPlugName = QStringLiteral("DeepSeek 翻译");
	pProcData->m_strComment  = QStringLiteral("选中文本后点角标，浮窗显示 DeepSeek 译文");
	pProcData->m_version     = QString::fromLatin1(kPluginVersion);
	pProcData->m_auther      = QStringLiteral("本机自建插件");

	// 1 = 自建二级菜单，我们要放"翻译选中 / 设置 / 关于"
	pProcData->m_menuType = 1;

	return true;
}

int NDD_PROC_MAIN(QWidget* pNotepad,
				  const QString& strFileName,
				  std::function<QsciScintilla*()> getCurEdit,
				  std::function<bool(int, void*)> pluginCallBack,
				  NDD_PROC_DATA* procData)
{
	Q_UNUSED(strFileName);
	Q_UNUSED(pluginCallBack);

#ifdef Q_OS_WIN
	pinModuleInProcess();
#endif

	if (procData == nullptr)
	{
		// m_menuType = 1 时主程序一定会传 procData；为 nullptr 说明调用方不是预期路径。
		return -1;
	}

	if (!getCurEdit)
	{
		// 拿不到"当前编辑器"的仿函数，插件无法工作
		return -1;
	}

	// 必须拷贝：主程序在调用返回后就会释放那个对象
	s_procData = *procData;
	s_mainWindow = pNotepad;

	QMenu* rootMenu = s_procData.m_rootMenu;
	if (rootMenu == nullptr)
	{
		return -1;
	}

	// ---- 1) 助手只建一次，但每次都要重绑到当前这个宿主窗口 ----
	// 助手挂在主窗口下面，不受插件菜单被 clear() 影响；
	// 但 ndd 每开一个新窗口都会再调一次 NDD_PROC_MAIN，那时必须重绑，
	// 否则助手会一直盯着第一个窗口，在别的窗口里就表现为"没有打开的编辑器"。
	if (s_assistant == nullptr)
	{
		// 以主窗口为 QObject 父对象：主窗口销毁时助手（连同浮窗）一起释放
		s_assistant = new SelectionAssistant(getCurEdit, pNotepad);
	}

	s_assistant->rebindHost(pNotepad, getCurEdit);

	// ---- 2) 菜单每次重建 ----
	buildPluginMenu(rootMenu);

	return 0;
}
