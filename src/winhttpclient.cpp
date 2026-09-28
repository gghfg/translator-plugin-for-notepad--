#include "winhttpclient.h"

#ifdef Q_OS_WIN

#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winhttp.h>

// 某些较老的 SDK 头里没有这个常量（值就是这个）
#  ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#    define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#  endif

#  include <string>

namespace {

// 句柄 RAII：WinHTTP 有很多提前 return 的分支，手动 close 很容易漏
class WinHttpHandle
{
public:
	WinHttpHandle() = default;
	explicit WinHttpHandle(HINTERNET handle) : m_handle(handle) {}

	~WinHttpHandle()
	{
		if (m_handle != nullptr)
		{
			::WinHttpCloseHandle(m_handle);
		}
	}

	WinHttpHandle(const WinHttpHandle&) = delete;
	WinHttpHandle& operator=(const WinHttpHandle&) = delete;

	// 允许移动：会话创建失败要退回另一种代理模式时会用到
	WinHttpHandle(WinHttpHandle&& other) noexcept : m_handle(other.m_handle)
	{
		other.m_handle = nullptr;
	}

	WinHttpHandle& operator=(WinHttpHandle&& other) noexcept
	{
		if (this != &other)
		{
			if (m_handle != nullptr)
			{
				::WinHttpCloseHandle(m_handle);
			}
			m_handle = other.m_handle;
			other.m_handle = nullptr;
		}
		return *this;
	}

	bool valid() const { return m_handle != nullptr; }
	operator HINTERNET() const { return m_handle; }

private:
	HINTERNET m_handle = nullptr;
};

// 系统给的原始错误文本（英文），作为兜底
QString systemMessage(DWORD code)
{
	wchar_t* buffer = nullptr;
	const DWORD length = ::FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

	QString text;
	if (length > 0 && buffer != nullptr)
	{
		text = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
	}
	if (buffer != nullptr)
	{
		::LocalFree(buffer);
	}
	return text;
}

// 把 WinHTTP 的错误码翻译成用户能看懂、能行动的说明。
//
// 这里用数字而不是 ERROR_WINHTTP_* 常量名：这些值在 ABI 上非常稳定，
// 而部分常量是较新 SDK 才补进 winhttp.h 的，写数字更不容易受 SDK 版本影响。
// 注释里标出对应常量名，便于对照 WINHTTP_ERROR_BASE(12000) + N。
QString friendlyError(DWORD code)
{
	QString hint;

	switch (code)
	{
	case 12002: // ERROR_WINHTTP_TIMEOUT
		hint = QStringLiteral("请求超时。可能是网络慢，或「设置」里的超时时间太短。");
		break;
	case 12005: // ERROR_WINHTTP_INVALID_URL
		hint = QStringLiteral("接口地址无效。请检查「设置」里的接口地址。");
		break;
	case 12006: // ERROR_WINHTTP_UNRECOGNIZED_SCHEME
		hint = QStringLiteral("接口地址的协议不支持，只支持 http:// 和 https://。");
		break;
	case 12007: // ERROR_WINHTTP_NAME_NOT_RESOLVED
		hint = QStringLiteral("域名解析失败。请检查网络、DNS，或是否配了代理。");
		break;
	case 12017: // ERROR_WINHTTP_OPERATION_CANCELLED
		hint = QStringLiteral("请求已取消。");
		break;
	case 12029: // ERROR_WINHTTP_CANNOT_CONNECT
		hint = QStringLiteral("连接不上服务器。可能是网络不通、被防火墙拦了，或需要走代理。");
		break;
	case 12030: // ERROR_WINHTTP_CONNECTION_ERROR
		hint = QStringLiteral("连接被中断。常见原因是代理设置不正确。");
		break;
	case 12037: // ERROR_WINHTTP_SECURE_CERT_DATE_INVALID
		hint = QStringLiteral("服务器证书已过期或尚未生效。请检查系统时间。");
		break;
	case 12038: // ERROR_WINHTTP_SECURE_CERT_CN_INVALID
		hint = QStringLiteral("服务器证书的域名不匹配。若你在走代理，请检查代理设置。");
		break;
	case 12045: // ERROR_WINHTTP_SECURE_INVALID_CA
		hint = QStringLiteral(
			"服务器证书的签发机构不受系统信任。若你在走代理或公司网络，"
			"很可能是代理在做 TLS 中间人；请换直连，或把代理的根证书装进系统信任区。");
		break;
	case 12057: // ERROR_WINHTTP_SECURE_CERT_REV_FAILED
		hint = QStringLiteral("无法检查证书的吊销状态（通常是连不上 OCSP/CRL 服务）。");
		break;
	case 12157: // ERROR_WINHTTP_SECURE_CHANNEL_ERROR
		hint = QStringLiteral("TLS 通道出错。可能是网络中间设备干扰，或系统加密组件异常。");
		break;
	case 12169: // ERROR_WINHTTP_SECURE_INVALID_CERT
		hint = QStringLiteral("服务器证书无效。");
		break;
	case 12170: // ERROR_WINHTTP_SECURE_CERT_REVOKED
		hint = QStringLiteral("服务器证书已被吊销。");
		break;
	case 12175: // ERROR_WINHTTP_SECURE_FAILURE
		hint = QStringLiteral(
			"TLS 握手失败。常见原因：系统时间不对、根证书缺失、"
			"或代理在做中间人劫持。");
		break;
	case 12185: // ERROR_WINHTTP_CLIENT_CERT_NO_PRIVATE_KEY
		// 注意：这个码在 WinHTTP 里报得比较"偏"——实测对着纯 HTTP 服务发 TLS 请求、
		// 以及经过 TLS 中间人时都会得到它。所以不要给过于具体的结论，只给方向性建议。
		hint = QStringLiteral(
			"TLS 建立失败。常见原因：代理或网络中间设备干扰、对方要求客户端证书、"
			"或对端根本不是 HTTPS 服务。可先关掉代理直连试试。");
		break;
	case 12188: // ERROR_WINHTTP_SECURE_FAILURE_PROXY
		hint = QStringLiteral("经代理建立 TLS 失败。请检查代理设置，或先关掉代理直连试试。");
		break;
	default:
		break;
	}

	const QString raw = systemMessage(code);
	const QString codeText = QStringLiteral("WinHTTP 错误 %1").arg(code);

	if (!hint.isEmpty())
	{
		return raw.isEmpty() ? QStringLiteral("%1：%2").arg(codeText, hint)
							 : QStringLiteral("%1：%2\n（系统原始信息：%3）")
								   .arg(codeText, hint, raw);
	}

	// 落在 WinHTTP 错误区间但没单独映射：给个方向性提示，别让用户只看到一串数字
	if (code >= 12000 && code <= 12200)
	{
		const QString generic = QStringLiteral(
			"网络层错误（可能是连接、代理或 TLS 证书问题）。"
			"可先试试关掉代理直连，并检查系统时间是否正确。");
		return raw.isEmpty() ? QStringLiteral("%1：%2").arg(codeText, generic)
							 : QStringLiteral("%1（%2）：%3").arg(codeText, generic, raw);
	}

	return raw.isEmpty() ? codeText : QStringLiteral("%1：%2").arg(codeText, raw);
}

// UTF-8 的 QString -> std::wstring，供 Win32 的 W 版接口使用
std::wstring toWide(const QString& text)
{
	return text.toStdWString();
}

} // namespace

namespace WinHttpTransport {

Response postJson(const QString& url,
				  const QByteArray& jsonBody,
				  const QString& bearerToken,
				  int timeoutMs)
{
	Response response;

	if (url.trimmed().isEmpty())
	{
		response.error = QStringLiteral("接口地址为空。");
		return response;
	}

	// ---- 拆 URL（顺便拿到要不要用 TLS）----
	std::wstring urlWide = toWide(url.trimmed());

	URL_COMPONENTS parts;
	::ZeroMemory(&parts, sizeof(parts));
	parts.dwStructSize = sizeof(parts);
	parts.dwSchemeLength = static_cast<DWORD>(-1);
	parts.dwHostNameLength = static_cast<DWORD>(-1);
	parts.dwUrlPathLength = static_cast<DWORD>(-1);
	parts.dwExtraInfoLength = static_cast<DWORD>(-1);

	if (!::WinHttpCrackUrl(urlWide.c_str(), static_cast<DWORD>(urlWide.size()), 0, &parts))
	{
		response.error = QStringLiteral("接口地址无法解析：%1").arg(url);
		return response;
	}

	std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
	std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
	if (parts.dwExtraInfoLength > 0 && parts.lpszExtraInfo != nullptr)
	{
		path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
	}
	if (path.empty())
	{
		path = L"/";
	}

	const bool secure = (parts.nScheme == INTERNET_SCHEME_HTTPS);

	// ---- 会话 ----
	// 先用"跟随系统代理设置"，失败再退回默认（不跟随）
	WinHttpHandle session(::WinHttpOpen(L"ndd-deepseek-translate/1.1",
										WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
										WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session.valid())
	{
		session = WinHttpHandle(::WinHttpOpen(L"ndd-deepseek-translate/1.1",
											  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
											  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	}
	if (!session.valid())
	{
		const DWORD code = ::GetLastError();
		response.error = QStringLiteral("初始化网络会话失败。\n%1").arg(friendlyError(code));
		return response;
	}

	const int timeout = (timeoutMs > 0) ? timeoutMs : 60000;
	::WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

	WinHttpHandle connection(::WinHttpConnect(session, host.c_str(),
											  static_cast<INTERNET_PORT>(parts.nPort), 0));
	if (!connection.valid())
	{
		const DWORD code = ::GetLastError();
		response.error = QStringLiteral("连接 %1 失败。\n%2")
							 .arg(QString::fromStdWString(host), friendlyError(code));
		return response;
	}

	WinHttpHandle request(::WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr,
											   WINHTTP_NO_REFERER,
											   WINHTTP_DEFAULT_ACCEPT_TYPES,
											   secure ? WINHTTP_FLAG_SECURE : 0));
	if (!request.valid())
	{
		const DWORD code = ::GetLastError();
		response.error = QStringLiteral("创建请求失败。\n%1").arg(friendlyError(code));
		return response;
	}

	// ---- 请求头 ----
	std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
	if (!bearerToken.isEmpty())
	{
		headers += L"Authorization: Bearer " + toWide(bearerToken) + L"\r\n";
	}

	if (!::WinHttpAddRequestHeaders(request, headers.c_str(),
									static_cast<DWORD>(headers.size()),
									WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
	{
		const DWORD code = ::GetLastError();
		response.error = QStringLiteral("设置请求头失败。\n%1").arg(friendlyError(code));
		return response;
	}

	// ---- 发送 ----
	if (!::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
							  const_cast<char*>(jsonBody.constData()),
							  static_cast<DWORD>(jsonBody.size()),
							  static_cast<DWORD>(jsonBody.size()), 0))
	{
		const DWORD code = ::GetLastError();
		response.error = QStringLiteral("发送请求失败。\n%1").arg(friendlyError(code));
		return response;
	}

	if (!::WinHttpReceiveResponse(request, nullptr))
	{
		const DWORD code = ::GetLastError();
		response.error = QStringLiteral("接收响应失败。\n%1").arg(friendlyError(code));
		return response;
	}

	// 拿到响应就算传输层通了，HTTP 状态码交给上层去解读
	response.transportOk = true;

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if (::WinHttpQueryHeaders(request,
							  WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
							  WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
							  WINHTTP_NO_HEADER_INDEX))
	{
		response.httpStatus = static_cast<int>(status);
	}

	// ---- 读响应体 ----
	for (;;)
	{
		DWORD available = 0;
		if (!::WinHttpQueryDataAvailable(request, &available))
		{
			response.error = QStringLiteral("读取响应长度失败。\n%1")
								 .arg(friendlyError(::GetLastError()));
			return response;
		}

		if (available == 0)
		{
			break; // 读完了
		}

		QByteArray chunk;
		chunk.resize(static_cast<int>(available));

		DWORD read = 0;
		if (!::WinHttpReadData(request, chunk.data(), available, &read))
		{
			response.error = QStringLiteral("读取响应内容失败。\n%1")
								 .arg(friendlyError(::GetLastError()));
			return response;
		}

		if (read == 0)
		{
			break;
		}

		chunk.resize(static_cast<int>(read));
		response.body += chunk;
	}

	return response;
}

} // namespace WinHttpTransport

#else // Q_OS_WIN

namespace WinHttpTransport {

Response postJson(const QString& url,
				  const QByteArray& jsonBody,
				  const QString& bearerToken,
				  int timeoutMs)
{
	Q_UNUSED(url);
	Q_UNUSED(jsonBody);
	Q_UNUSED(bearerToken);
	Q_UNUSED(timeoutMs);

	Response response;
	response.error = QStringLiteral("本插件的传输层基于 WinHTTP，仅在 Windows 上可用。");
	return response;
}

} // namespace WinHttpTransport

#endif // Q_OS_WIN
