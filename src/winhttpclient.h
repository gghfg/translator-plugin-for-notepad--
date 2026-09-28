#pragma once

// 用 Windows 自带的 WinHTTP 发 HTTPS 请求，替代 Qt 的 QNetworkAccessManager。
//
// 为什么要换掉 Qt 那条路：
//   Qt 5.15 在 Windows 桌面上只支持 OpenSSL 后端，会写死去找
//   libssl-1_1-x64.dll / libcrypto-1_1-x64.dll。于是插件平白多出一堆外部依赖问题：
//   缺文件、架构不对、OpenSSL 3.x 名字不匹配、以及从别的软件目录"提取"出来的 DLL
//   带着私有 CRT 并排程序集清单（实测撞到过 Avast.VC140.CRT，LoadLibrary 直接报
//   14001）。而且 OpenSSL 1.1 已经 EOL。
//
//   WinHTTP 走 Windows 自带的 Schannel：不需要任何外部 DLL，TLS 由系统维护，
//   也不受宿主 Qt 是怎么构建的影响。
//
// 本接口是**同步**的，必须在工作线程里调用（见 DeepSeekClient 里用 QtConcurrent 的用法）。

#include <QByteArray>
#include <QString>

namespace WinHttpTransport
{

struct Response
{
	// 传输层是否走通。只要拿到了 HTTP 响应（哪怕是 4xx/5xx）就算 true；
	// DNS 失败、连不上、超时、TLS 握手失败等才是 false。
	bool transportOk = false;

	// HTTP 状态码；传输层失败时为 0
	int httpStatus = 0;

	// 响应体原文（交给上层的 JSON 解析处理）
	QByteArray body;

	// 传输层错误的可读说明；transportOk 为 true 时为空
	QString error;
};

// 对一个 JSON 接口发 POST 请求。timeoutMs 同时用作解析/连接/发送/接收四个超时。
Response postJson(const QString& url,
				  const QByteArray& jsonBody,
				  const QString& bearerToken,
				  int timeoutMs);

} // namespace WinHttpTransport
