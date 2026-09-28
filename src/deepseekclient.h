#pragma once

// DeepSeek Chat Completions 客户端。
//
// 传输层用 **WinHTTP**（Windows 自带的 Schannel），不再走 Qt 的 QNetworkAccessManager。
// 原因见 winhttpclient.h 顶部：Qt 5.15 在 Windows 上只认 OpenSSL 1.1，会平白引入
// 一堆外部依赖问题（缺 DLL、架构不对、OpenSSL 3.x 名字不匹配、DLL 带私有 CRT
// 并排程序集清单导致 LoadLibrary 报 14001……），而 OpenSSL 1.1 已经 EOL。
//
// WinHTTP 是同步接口，所以请求放到线程池里跑，结果通过 QFutureWatcher 回到
// 本对象所在的线程。任何情况下都必须发出 finished 或 failed，
// 否则界面会一直停在"翻译中"。

#include <QByteArray>
#include <QObject>
#include <QString>

#include "translatorconfig.h"
#include "winhttpclient.h"

class DeepSeekClient : public QObject
{
	Q_OBJECT

public:
	explicit DeepSeekClient(QObject* parent = nullptr);
	~DeepSeekClient() override;

	void setConfig(const TranslatorConfig& config);

	void translate(const QString& text);
	void cancel();

	bool isBusy() const { return m_busy; }

	// 供"诊断"用：一行说明当前传输层
	static QString transportInfo();

signals:
	void finished(const QString& translated);
	void failed(const QString& error);
	void busyChanged(bool busy);

private:
	void handleResponse(const WinHttpTransport::Response& response);
	void setBusy(bool busy);

	static QByteArray buildRequestBody(const TranslatorConfig& config, const QString& text);
	static QString    buildUserPrompt(const TranslatorConfig& config, const QString& text);
	static QString    extractContent(const QByteArray& body, QString* errorOut);
	static QString    extractErrorMessage(const QByteArray& body);

	TranslatorConfig m_config;

	// 每次请求递增。只有序号仍是最新的结果才允许交付给界面，
	// 这样"翻译中再次触发"时，上一次迟到的结果会被丢掉。
	quint64 m_generation = 0;

	bool m_busy = false;
};
