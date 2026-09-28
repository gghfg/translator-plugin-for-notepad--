#include "translatorconfig.h"

#include <QByteArray>
#include <QFileInfo>
#include <QSettings>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <wincrypt.h>
#endif

const char* TranslatorConfig::kDefaultBaseUrl = "https://api.deepseek.com";
const char* TranslatorConfig::kDefaultModel   = "deepseek-chat";

// 这段提示词是译文质量的关键：明确"只输出译文"能避免模型加解释，
// 而划词翻译最怕的就是模型在译文前后加一句"以下是翻译："。
const char* TranslatorConfig::kDefaultSystemPrompt =
	"你是一个专业的翻译引擎。规则：\n"
	"1. 只输出译文本身，不要输出任何解释、说明、前后缀、引号或 Markdown 代码块标记。\n"
	"2. 保留原文中的代码片段、变量名、URL、文件路径和占位符（如 %s、{0}）不翻译。\n"
	"3. 如果原文本身已经是目标语言，则原样返回。";

namespace {

const char* const kPlacementCorner    = "corner";
const char* const kPlacementSelection = "selection";

} // namespace

// ---------------------------------------------------------------- 加密存储

// 存储格式：Windows 为 "dpapi:" + Base64(CryptProtectData 结果)；
// 其它平台为 "b64:" + Base64（仅混淆，不是加密，设置界面会提示）。

#ifdef Q_OS_WIN

static QString dpapiProtect(const QByteArray& data)
{
	DATA_BLOB in;
	in.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(data.constData()));
	in.cbData = static_cast<DWORD>(data.size());

	DATA_BLOB out;
	out.pbData = nullptr;
	out.cbData = 0;

	// CRYPTPROTECT_UI_FORBIDDEN：不要弹任何系统对话框，避免卡住编辑器
	if (!CryptProtectData(&in, L"ndd-deepseek-translate", nullptr, nullptr, nullptr,
						  CRYPTPROTECT_UI_FORBIDDEN, &out))
	{
		return QString();
	}

	const QByteArray packed(reinterpret_cast<const char*>(out.pbData),
							static_cast<int>(out.cbData));
	LocalFree(out.pbData);

	return QStringLiteral("dpapi:") + QString::fromLatin1(packed.toBase64());
}

static QString dpapiUnprotect(const QString& payload)
{
	const QByteArray packed = QByteArray::fromBase64(payload.toLatin1());
	if (packed.isEmpty())
	{
		return QString();
	}

	DATA_BLOB in;
	in.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(packed.constData()));
	in.cbData = static_cast<DWORD>(packed.size());

	DATA_BLOB out;
	out.pbData = nullptr;
	out.cbData = 0;

	if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
							CRYPTPROTECT_UI_FORBIDDEN, &out))
	{
		return QString();
	}

	const QByteArray plain(reinterpret_cast<const char*>(out.pbData),
						   static_cast<int>(out.cbData));
	LocalFree(out.pbData);

	return QString::fromUtf8(plain);
}

#endif // Q_OS_WIN

QString TranslatorConfig::protectSecret(const QString& plain)
{
	if (plain.isEmpty())
	{
		return QString();
	}

	const QByteArray raw = plain.toUtf8();

#ifdef Q_OS_WIN
	const QString protectedValue = dpapiProtect(raw);
	if (!protectedValue.isEmpty())
	{
		return protectedValue;
	}
	// DPAPI 失败时退化为混淆，至少不是裸明文
#endif

	return QStringLiteral("b64:") + QString::fromLatin1(raw.toBase64());
}

QString TranslatorConfig::unprotectSecret(const QString& stored)
{
	if (stored.isEmpty())
	{
		return QString();
	}

	if (stored.startsWith(QStringLiteral("dpapi:")))
	{
#ifdef Q_OS_WIN
		return dpapiUnprotect(stored.mid(6));
#else
		// 配置从 Windows 拷过来时无法解密，只能要求重新填写
		return QString();
#endif
	}

	if (stored.startsWith(QStringLiteral("b64:")))
	{
		return QString::fromUtf8(QByteArray::fromBase64(stored.mid(4).toLatin1()));
	}

	// 既没有前缀也不是空：当作用户手写的明文处理
	return stored;
}

// ---------------------------------------------------------------- 读写

QString TranslatorConfig::filePath()
{
	// 刻意用 QSettings(UserScope + IniFormat + 组织名 "notepad") 来定位配置文件，
	// 而不是拼 QStandardPaths：ndd 自己就是这么存 nddsets.ini 的
	// （%APPDATA%\notepad\nddsets.ini），两边放在同一目录，用户好找。
	QSettings settings(QSettings::IniFormat, QSettings::UserScope,
					   QStringLiteral("notepad"), QStringLiteral("deepseek-translate"));
	return settings.fileName();
}

int TranslatorConfig::maxSelectionChars()
{
	// 划词翻译面向的是"一段文本"。选中整篇大文件时既慢又贵，
	// 超过这个长度直接拒绝并提示，比默默发出一个巨大请求要好。
	return 20000;
}

TranslatorConfig::TranslatorConfig()
	: apiKey()
	, baseUrl(QString::fromLatin1(kDefaultBaseUrl))
	, model(QString::fromLatin1(kDefaultModel))
	, temperature(1.0)
	, systemPrompt(QString::fromUtf8(kDefaultSystemPrompt))
	, sourceLang(QStringLiteral("自动检测"))
	, targetLang(QStringLiteral("中文"))
	, timeoutMs(60000)
	, buttonPlacement(ButtonPlacement::EditorCorner)
{
}

bool TranslatorConfig::load()
{
	QSettings s(QSettings::IniFormat, QSettings::UserScope,
				QStringLiteral("notepad"), QStringLiteral("deepseek-translate"));

	if (!QFileInfo::exists(s.fileName()))
	{
		normalize();
		return false;
	}

	s.setIniCodec("UTF-8");

	const QString storedKey = s.value(QStringLiteral("apiKeyProtected")).toString();
	if (!storedKey.isEmpty())
	{
		apiKey = unprotectSecret(storedKey);
	}
	if (apiKey.isEmpty())
	{
		// 允许用户直接用记事本把 key 写进 ini
		apiKey = s.value(QStringLiteral("apiKeyPlain")).toString();
	}

	baseUrl      = s.value(QStringLiteral("baseUrl"), baseUrl).toString();
	model        = s.value(QStringLiteral("model"), model).toString();
	temperature  = s.value(QStringLiteral("temperature"), temperature).toDouble();
	systemPrompt = s.value(QStringLiteral("systemPrompt"), systemPrompt).toString();
	sourceLang   = s.value(QStringLiteral("sourceLang"), sourceLang).toString();
	targetLang   = s.value(QStringLiteral("targetLang"), targetLang).toString();
	timeoutMs    = s.value(QStringLiteral("timeoutMs"), timeoutMs).toInt();

	const QString placement =
		s.value(QStringLiteral("buttonPlacement"), QString::fromLatin1(kPlacementCorner)).toString();
	buttonPlacement = (placement == QString::fromLatin1(kPlacementSelection))
						  ? ButtonPlacement::SelectionEnd
						  : ButtonPlacement::EditorCorner;

	if (baseUrl.trimmed().isEmpty())
	{
		baseUrl = QString::fromLatin1(kDefaultBaseUrl);
	}
	if (model.trimmed().isEmpty())
	{
		model = QString::fromLatin1(kDefaultModel);
	}
	if (systemPrompt.trimmed().isEmpty())
	{
		systemPrompt = QString::fromUtf8(kDefaultSystemPrompt);
	}

	normalize();
	return true;
}

bool TranslatorConfig::save() const
{
	// QSettings 会自行创建 %APPDATA%\notepad 目录，不需要手动 mkpath
	QSettings s(QSettings::IniFormat, QSettings::UserScope,
				QStringLiteral("notepad"), QStringLiteral("deepseek-translate"));
	s.setIniCodec("UTF-8");

	s.setValue(QStringLiteral("apiKeyProtected"), protectSecret(apiKey));
	s.remove(QStringLiteral("apiKeyPlain")); // 不再保留明文副本

	s.setValue(QStringLiteral("baseUrl"), baseUrl);
	s.setValue(QStringLiteral("model"), model);
	s.setValue(QStringLiteral("temperature"), temperature);
	s.setValue(QStringLiteral("systemPrompt"), systemPrompt);
	s.setValue(QStringLiteral("sourceLang"), sourceLang);
	s.setValue(QStringLiteral("targetLang"), targetLang);
	s.setValue(QStringLiteral("timeoutMs"), timeoutMs);
	s.setValue(QStringLiteral("buttonPlacement"),
			   QString::fromLatin1(buttonPlacement == ButtonPlacement::SelectionEnd
									   ? kPlacementSelection
									   : kPlacementCorner));

	s.sync();
	return s.status() == QSettings::NoError;
}

QString TranslatorConfig::chatCompletionsUrl() const
{
	QString base = baseUrl.trimmed();
	while (base.endsWith(QLatin1Char('/')))
	{
		base.chop(1);
	}

	// 允许用户填 https://api.deepseek.com 或 https://api.deepseek.com/v1
	if (base.endsWith(QStringLiteral("/chat/completions")))
	{
		return base;
	}

	return base + QStringLiteral("/chat/completions");
}

void TranslatorConfig::normalize()
{
	if (temperature < 0.0) { temperature = 0.0; }
	if (temperature > 2.0) { temperature = 2.0; }

	if (timeoutMs < 5000)   { timeoutMs = 5000; }
	if (timeoutMs > 300000) { timeoutMs = 300000; }

	if (sourceLang.trimmed().isEmpty()) { sourceLang = QStringLiteral("自动检测"); }
	if (targetLang.trimmed().isEmpty()) { targetLang = QStringLiteral("中文"); }
}
