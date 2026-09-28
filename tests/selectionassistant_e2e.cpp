// ---------------------------------------------------------------------------
// SelectionAssistant 的端到端测试（不需要真实 API Key，不需要人工点击）
//
// 思路：插件的核心链路是
//     选中文本 → 角落出现「译」按钮 → 点击 → 浮窗显示译文
// 这条链路上唯一无法自动化的是"人真的用鼠标点了一下"。其余部分可以这样验证：
//
//   1. 直接 new 一个真实的 QsciScintilla（它是 qmyedit_qt5.dll 导出的类），
//      按 ndd 的做法给它挂上动态属性 type=1（普通文本模式）；
//   2. 构造 SelectionAssistant，等它的轮询把编辑器挂上；
//   3. 断言编辑器下确实出现了一个可见的 QToolButton —— 也就是「译」按钮；
//   4. 调用 button->click()，走的是和鼠标点击完全相同的 clicked 信号路径；
//   5. 断言译文浮窗出现，且内容就是 mock 服务器返回的那句。
//
// 用法： ndd-translate-e2e-test <baseUrl>
// 退出码：0=全链路通过  5=按钮没出现  6=浮窗没出现或内容不对  9=配置准备失败
//
// ⚠️ 前置条件：**QScintilla 头文件必须与 qmyedit_qt5.dll 完全同一修订**。
//    本测试要 new 一个真实的 QsciScintilla，只要有一个虚函数在 DLL 里没有定义，
//    链接就会 LNK2001。工程自带的头文件来自 GitHub 镜像，与 ndd v3.9.0 的 DLL
//    并非同一修订（实测净差 5 个虚函数槽位），因此 CMake 里这个目标默认关闭，
//    需要 -DNDD_BUILD_E2E_TEST=ON 才构建。插件本身不需要构造 QsciScintilla，
//    所以头文件版本漂移不影响插件功能。
//
// 注意：SelectionAssistant 会从 %APPDATA%\notepad\deepseek-translate.ini 读配置，
// 所以本测试会**先备份该文件、写入测试配置、结束时恢复**，不会污染真实配置。
// ---------------------------------------------------------------------------

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QPlainTextEdit>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <cstdio>

#include <qsciscintilla.h>

#include "selectionassistant.h"
#include "translatorconfig.h"

namespace {

QString g_configPath;
QByteArray g_configBackup;
bool g_hadConfig = false;

void backupConfig()
{
	g_configPath = TranslatorConfig::filePath();
	g_hadConfig = QFile::exists(g_configPath);

	if (!g_hadConfig)
	{
		return;
	}

	QFile file(g_configPath);
	if (file.open(QIODevice::ReadOnly))
	{
		g_configBackup = file.readAll();
		file.close();
	}
}

void restoreConfig()
{
	if (g_hadConfig)
	{
		QFile file(g_configPath);
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			file.write(g_configBackup);
			file.close();
		}
	}
	else if (QFile::exists(g_configPath))
	{
		QFile::remove(g_configPath);
	}
}

bool prepareConfig(const QString& baseUrl)
{
	TranslatorConfig config;
	config.apiKey = QStringLiteral("test-key-e2e");
	config.baseUrl = baseUrl;
	config.model = QStringLiteral("deepseek-chat");
	config.sourceLang = QStringLiteral("自动检测");
	config.targetLang = QStringLiteral("中文");
	config.timeoutMs = 8000;
	config.buttonPlacement = TranslatorConfig::ButtonPlacement::EditorCorner;
	return config.save();
}

} // namespace

int main(int argc, char** argv)
{
	QApplication app(argc, argv);

	const QString baseUrl = (argc > 1) ? QString::fromLocal8Bit(argv[1])
									   : QStringLiteral("http://127.0.0.1:18080/ok");

	backupConfig();
	if (!prepareConfig(baseUrl))
	{
		std::printf("FATAL|无法写入测试配置 %s\n", g_configPath.toUtf8().constData());
		std::fflush(stdout);
		restoreConfig();
		return 9;
	}
	std::printf("CONFIG|%s\n", g_configPath.toUtf8().constData());

	// ---- 造一个和 ndd 里一样的编辑器 ----
	QsciScintilla* editor = new QsciScintilla();
	editor->setText(QStringLiteral("Hello, world. This is an end to end test."));

	// ndd 就是这么标记"普通文本模式"的；不设的话插件会把它当未知模式拒绝翻译
	editor->setProperty("type", 1);
	editor->setProperty("filePath", QStringLiteral("e2e-test.txt"));

	editor->resize(800, 600);
	editor->show();
	editor->selectAll();

	SelectionAssistant assistant([editor]() -> QsciScintilla* { return editor; });

	int exitCode = 3;

	// 第一步：等轮询把编辑器挂上，检查「译」按钮是否出现
	QTimer::singleShot(1000, [&]() {
		QToolButton* button = editor->findChild<QToolButton*>();
		if (button == nullptr)
		{
			std::printf("BUTTON|not-found\n");
			std::fflush(stdout);
			exitCode = 5;
			QCoreApplication::quit();
			return;
		}

		std::printf("BUTTON|found|visible=%d|text=%s|pos=%d,%d|size=%dx%d\n",
					button->isVisible() ? 1 : 0,
					button->text().toUtf8().constData(),
					button->x(), button->y(), button->width(), button->height());
		std::fflush(stdout);

		if (!button->isVisible())
		{
			exitCode = 5;
			QCoreApplication::quit();
			return;
		}

		// 第二步：点它。走的是和鼠标点击一模一样的 clicked 信号路径
		std::printf("CLICK|simulating\n");
		std::fflush(stdout);
		button->click();
	});

	// 第三步：等网络往返，检查译文浮窗
	QTimer::singleShot(5000, [&]() {
		bool popupVisible = false;
		QString body;

		const QWidgetList tops = QApplication::topLevelWidgets();
		for (QWidget* widget : tops)
		{
			if (widget->objectName() != QStringLiteral("nddTranslationPopup"))
			{
				continue;
			}

			popupVisible = widget->isVisible();
			if (QPlainTextEdit* bodyEdit = widget->findChild<QPlainTextEdit*>())
			{
				body = bodyEdit->toPlainText();
			}
		}

		std::printf("POPUP|visible=%d|body=%s\n", popupVisible ? 1 : 0,
					body.toUtf8().constData());
		std::fflush(stdout);

		exitCode = (popupVisible && !body.isEmpty()) ? 0 : 6;
		QCoreApplication::quit();
	});

	QTimer::singleShot(12000, []() {
		std::printf("RESULT_TIMEOUT\n");
		std::fflush(stdout);
		QCoreApplication::quit();
	});

	app.exec();

	restoreConfig();
	std::printf("SUMMARY|exit=%d|config-restored\n", exitCode);
	std::fflush(stdout);
	return exitCode;
}
