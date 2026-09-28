#include "deepseekclient.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QtConcurrent/QtConcurrentRun>

namespace {

// 把常见的 HTTP 状态码翻译成用户能看懂、能行动的提示。
QString friendlyHint(int status)
{
	switch (status)
	{
	case 400: return QStringLiteral("请求格式被拒绝，通常是模型名填错了。");
	case 401: return QStringLiteral("API Key 无效或已过期，请在“设置”里重新填写。");
	case 402: return QStringLiteral("账户余额不足，请先到 DeepSeek 平台充值。");
	case 403: return QStringLiteral("没有访问该模型的权限。");
	case 429: return QStringLiteral("请求过于频繁，请稍后重试。");
	case 500:
	case 502:
	case 503:
	case 504: return QStringLiteral("DeepSeek 服务端暂时不可用，请稍后重试。");
	default:  return QString();
	}
}

} // namespace

DeepSeekClient::DeepSeekClient(QObject* parent)
	: QObject(parent)
{
}

DeepSeekClient::~DeepSeekClient() = default;

void DeepSeekClient::setConfig(const TranslatorConfig& config)
{
	m_config = config;
}

QString DeepSeekClient::transportInfo()
{
#ifdef Q_OS_WIN
	return QStringLiteral("WinHTTP + Schannel（Windows 自带，无需 OpenSSL 等外部库）");
#else
	return QStringLiteral("WinHTTP（仅 Windows 可用）");
#endif
}

void DeepSeekClient::translate(const QString& text)
{
	if (m_busy)
	{
		cancel();
	}

	if (text.trimmed().isEmpty())
	{
		emit failed(QStringLiteral("没有可翻译的内容。"));
		return;
	}

	if (m_config.apiKey.trimmed().isEmpty())
	{
		emit failed(QStringLiteral("尚未配置 API Key，请在“设置”里填写。"));
		return;
	}

	const QString    url = m_config.chatCompletionsUrl();
	const QByteArray body = buildRequestBody(m_config, text);
	const QString    token = m_config.apiKey;
	const int        timeoutMs = m_config.timeoutMs;

	const quint64 generation = ++m_generation;

	setBusy(true);

	// 每个请求配一个自己的 watcher：这样"序号"是按请求捕获的，
	// 不会出现共享 watcher 时"旧结果撞上新序号"的竞态。
	QFutureWatcher<WinHttpTransport::Response>* watcher =
		new QFutureWatcher<WinHttpTransport::Response>(this);

	connect(watcher, &QFutureWatcher<WinHttpTransport::Response>::finished,
			this, [this, watcher, generation]() {
				const WinHttpTransport::Response response = watcher->result();
				watcher->deleteLater();

				// 期间被取消、或又发起了新请求 —— 这个结果已经过期，丢掉
				if (generation != m_generation)
				{
					return;
				}

				handleResponse(response);
			});

	// WinHTTP 是同步的，扔到线程池里跑，避免卡住界面。
	// lambda 只按值捕获，完全不碰 this —— 即使本对象先被销毁也不会悬空。
	watcher->setFuture(QtConcurrent::run([url, body, token, timeoutMs]() {
		return WinHttpTransport::postJson(url, body, token, timeoutMs);
	}));
}

void DeepSeekClient::handleResponse(const WinHttpTransport::Response& response)
{
	setBusy(false);

	// 传输层没走通：直接报可读原因，没有 HTTP 状态码可解读
	if (!response.transportOk)
	{
		emit failed(response.error.isEmpty() ? QStringLiteral("网络请求失败。")
											 : response.error);
		return;
	}

	QString parseError;
	const QString content = extractContent(response.body, &parseError);

	if (parseError.isEmpty())
	{
		emit finished(content);
		return;
	}

	// 优先用接口返回的 error.message，它比"JSON 解析失败"更有信息量
	QString message = parseError;
	const QString apiMessage = extractErrorMessage(response.body);
	if (!apiMessage.isEmpty())
	{
		message = apiMessage;
	}

	if (response.httpStatus > 0)
	{
		message = QStringLiteral("HTTP %1：%2").arg(response.httpStatus).arg(message);
	}

	const QString hint = friendlyHint(response.httpStatus);
	if (!hint.isEmpty())
	{
		message += QStringLiteral("\n") + hint;
	}

	emit failed(message);
}

void DeepSeekClient::cancel()
{
	// 不去强行中断已经在跑的 WinHTTP 请求（那需要跨线程关句柄，容易出问题），
	// 而是把序号推进一格：请求跑完回来时会被判定为过期并丢弃。
	// 超时本身由 WinHttpSetTimeouts 兜着，不会无限占用线程。
	++m_generation;

	setBusy(false);
}

void DeepSeekClient::setBusy(bool busy)
{
	if (m_busy == busy)
	{
		return;
	}

	m_busy = busy;
	emit busyChanged(busy);
}

// ------------------------------------------------------------------ 请求组装

QByteArray DeepSeekClient::buildRequestBody(const TranslatorConfig& config, const QString& text)
{
	QJsonObject systemMessage;
	systemMessage.insert(QStringLiteral("role"), QStringLiteral("system"));
	systemMessage.insert(QStringLiteral("content"), config.systemPrompt);

	QJsonObject userMessage;
	userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
	userMessage.insert(QStringLiteral("content"), buildUserPrompt(config, text));

	QJsonArray messages;
	messages.append(systemMessage);
	messages.append(userMessage);

	QJsonObject body;
	body.insert(QStringLiteral("model"), config.model);
	body.insert(QStringLiteral("messages"), messages);
	body.insert(QStringLiteral("stream"), false);

	// deepseek-reasoner 不支持 temperature，传了会被忽略甚至直接报错，所以不发送
	if (!config.model.contains(QStringLiteral("reasoner"), Qt::CaseInsensitive))
	{
		body.insert(QStringLiteral("temperature"), config.temperature);
	}

	return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QString DeepSeekClient::buildUserPrompt(const TranslatorConfig& config, const QString& text)
{
	QString head;
	if (config.sourceLang.trimmed().isEmpty() ||
		config.sourceLang == QStringLiteral("自动检测"))
	{
		head = QStringLiteral("把下面的文本翻译成%1。").arg(config.targetLang);
	}
	else
	{
		head = QStringLiteral("把下面的文本从%1翻译成%2。")
				   .arg(config.sourceLang, config.targetLang);
	}

	return head + QStringLiteral("\n\n") + text;
}

// ------------------------------------------------------------------ 解析

QString DeepSeekClient::extractContent(const QByteArray& body, QString* errorOut)
{
	if (errorOut != nullptr)
	{
		errorOut->clear();
	}

	QJsonParseError parseError{};
	const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);

	if (parseError.error != QJsonParseError::NoError)
	{
		if (errorOut != nullptr)
		{
			*errorOut = QStringLiteral("接口返回的不是合法 JSON：%1").arg(parseError.errorString());
		}
		return QString();
	}

	if (!doc.isObject())
	{
		if (errorOut != nullptr)
		{
			*errorOut = QStringLiteral("接口返回的 JSON 顶层不是对象。");
		}
		return QString();
	}

	const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
	if (choices.isEmpty())
	{
		if (errorOut != nullptr)
		{
			const QString apiMessage = extractErrorMessage(body);
			*errorOut = apiMessage.isEmpty()
						   ? QStringLiteral("接口没有返回任何结果（choices 为空）。")
						   : apiMessage;
		}
		return QString();
	}

	const QJsonObject message =
		choices.at(0).toObject().value(QStringLiteral("message")).toObject();
	const QString content = message.value(QStringLiteral("content")).toString();

	if (content.trimmed().isEmpty())
	{
		if (errorOut != nullptr)
		{
			*errorOut = QStringLiteral("接口返回了空的译文。");
		}
		return QString();
	}

	return content;
}

QString DeepSeekClient::extractErrorMessage(const QByteArray& body)
{
	const QJsonDocument doc = QJsonDocument::fromJson(body);
	if (!doc.isObject())
	{
		return QString();
	}

	const QJsonObject error = doc.object().value(QStringLiteral("error")).toObject();
	if (error.isEmpty())
	{
		return QString();
	}

	QString message = error.value(QStringLiteral("message")).toString().trimmed();
	const QString type = error.value(QStringLiteral("type")).toString().trimmed();

	if (!type.isEmpty() && !message.contains(type))
	{
		message = message.isEmpty() ? type : QStringLiteral("%1（%2）").arg(message, type);
	}

	return message;
}
