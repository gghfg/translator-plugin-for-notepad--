// ---------------------------------------------------------------------------
// DeepSeekClient 的本地联调测试（不访问真实接口，不需要 API Key）
//
// 为什么要它：插件的功能核心是"发请求 → 解析 → 把结果或错误交给界面"。
// 这部分有真实分支（鉴权头格式、请求体字段、choices 解析、HTTP 状态码 → 人话提示，
// 以及"翻译中再次触发会不会取消上一次"），但界面交互需要 API Key 才能触发。
// 这个程序把 DeepSeekClient 单独拎出来对着本地 mock 服务器跑。
//
// 用法：
//   ndd-translate-client-test <baseUrl> [场景]
//   场景：ok（默认） / unauthorized / badjson / emptychoices / cancel
//
// 退出码：0=符合预期  2=预期外的 failed  3=超时/没结果  4=cancel 场景结果数不对
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QString>
#include <QTimer>
#include <cstdio>

#include "deepseekclient.h"
#include "translatorconfig.h"

namespace {

int g_okCount = 0;
int g_failCount = 0;

// 收到结果后不立刻退出：留一小段时间，看会不会再冒出一个结果。
// cancel 场景正是要验证"被取消的那次不会再回来"。
void scheduleQuit(QCoreApplication& app)
{
	QTimer::singleShot(1200, &app, []() { QCoreApplication::quit(); });
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QString baseUrl = (argc > 1) ? QString::fromLocal8Bit(argv[1])
									   : QStringLiteral("http://127.0.0.1:18080/ok");
	const QString scenario = (argc > 2) ? QString::fromLocal8Bit(argv[2])
										: QStringLiteral("ok");

	TranslatorConfig config;
	config.baseUrl = baseUrl;
	config.apiKey = QStringLiteral("test-key-should-appear-as-bearer");
	config.model = QStringLiteral("deepseek-chat");
	config.sourceLang = QStringLiteral("自动检测");
	config.targetLang = QStringLiteral("中文");
	config.temperature = 1.0;
	config.timeoutMs = 8000;

	DeepSeekClient client;
	client.setConfig(config);

	QObject::connect(&client, &DeepSeekClient::finished, [&app](const QString& translated) {
		++g_okCount;
		std::printf("RESULT_OK|%s\n", translated.toUtf8().constData());
		std::fflush(stdout);
		scheduleQuit(app);
	});

	QObject::connect(&client, &DeepSeekClient::failed, [&app](const QString& error) {
		++g_failCount;
		std::printf("RESULT_FAIL|%s\n", error.toUtf8().constData());
		std::fflush(stdout);
		scheduleQuit(app);
	});

	QObject::connect(&client, &DeepSeekClient::busyChanged, [](bool busy) {
		std::printf("BUSY|%d\n", busy ? 1 : 0);
		std::fflush(stdout);
	});

	std::printf("REQ|url=%s|scenario=%s\n", config.chatCompletionsUrl().toUtf8().constData(),
				scenario.toUtf8().constData());
	std::fflush(stdout);

	if (scenario == QStringLiteral("cancel"))
	{
		// 第一次请求打到 /slow（服务器会拖 2.5 秒），300ms 后再来一次快的。
		// 期望：只有快的那次给出结果，被取消的那次不再产生任何信号。
		client.translate(QStringLiteral("SLOW：这一次应该被取消"));
		QTimer::singleShot(300, &client, [&client]() {
			client.translate(QStringLiteral("fast：这一次应该成功"));
		});
	}
	else
	{
		client.translate(QStringLiteral("Hello, world. This is a smoke test."));
	}

	// 兜底：万一既没 finished 也没 failed，别让测试挂死
	QTimer::singleShot(15000, &app, []() {
		std::printf("RESULT_TIMEOUT\n");
		std::fflush(stdout);
		QCoreApplication::quit();
	});

	app.exec();

	std::printf("SUMMARY|ok=%d|fail=%d\n", g_okCount, g_failCount);
	std::fflush(stdout);

	if (scenario == QStringLiteral("cancel"))
	{
		return (g_okCount == 1 && g_failCount == 0) ? 0 : 4;
	}
	if (g_okCount == 1 && g_failCount == 0)
	{
		return 0;
	}
	if (g_failCount == 1 && g_okCount == 0)
	{
		return 2;
	}
	return 3;
}
