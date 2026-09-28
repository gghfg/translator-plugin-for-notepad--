#pragma once

// 插件配置：读写 + API Key 加密存储。
//
// 配置文件位置：由 QSettings(UserScope + IniFormat + 组织名 "notepad") 决定，
// 即 %APPDATA%\notepad\deepseek-translate.ini —— 和 ndd 自己存 nddsets.ini 的位置一致。
//
// API Key 不明文落盘：Windows 下用 DPAPI（CryptProtectData，绑定当前用户）加密后
// Base64 存储；非 Windows 平台退化为 Base64 混淆并在设置界面提示。

#include <QString>

class TranslatorConfig
{
public:
	// 悬浮按钮出现的位置
	enum class ButtonPlacement
	{
		EditorCorner,  // 编辑器右下角（默认）
		SelectionEnd   // 贴着选区末尾，像划词翻译那样
	};

	TranslatorConfig();

	bool load();
	bool save() const;

	static QString filePath();
	static QString protectSecret(const QString& plain);
	static QString unprotectSecret(const QString& stored);

	QString chatCompletionsUrl() const;
	void    normalize();

	// 单个选区最多允许多少个字符。超出就拒绝，避免误选中整篇大文件时
	// 发出一个巨大（且昂贵）的请求。
	static int maxSelectionChars();

	static const char* kDefaultBaseUrl;
	static const char* kDefaultModel;
	static const char* kDefaultSystemPrompt;

public:
	QString         apiKey;
	QString         baseUrl;
	QString         model;
	double          temperature;
	QString         systemPrompt;
	QString         sourceLang;
	QString         targetLang;
	int             timeoutMs;
	ButtonPlacement buttonPlacement;
};
