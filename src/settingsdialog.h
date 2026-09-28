#pragma once

// 设置对话框：API Key / 模型 / 语言 / 提示词 / 按钮位置。

#include <QDialog>

#include "translatorconfig.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class DeepSeekClient;

class SettingsDialog : public QDialog
{
	Q_OBJECT

public:
	explicit SettingsDialog(const TranslatorConfig& config, QWidget* parent = nullptr);
	~SettingsDialog() override;

	// 用户点"保存"后取回编辑结果
	TranslatorConfig config() const;

private slots:
	void toggleKeyVisible();
	void restoreDefaults();
	void testConnection();
	void onTestFinished(const QString& translated);
	void onTestFailed(const QString& error);

private:
	QWidget*         buildForm();
	void             applyConfigToUi();
	TranslatorConfig readUi() const;
	void             setTestBusy(bool busy);

	TranslatorConfig m_config;
	DeepSeekClient*  m_testClient = nullptr;

	QLineEdit*      m_keyEdit = nullptr;
	QPushButton*    m_keyToggle = nullptr;
	QLineEdit*      m_baseUrlEdit = nullptr;
	QComboBox*      m_modelCombo = nullptr;
	QDoubleSpinBox* m_temperatureSpin = nullptr;
	QComboBox*      m_sourceCombo = nullptr;
	QComboBox*      m_targetCombo = nullptr;
	QComboBox*      m_placementCombo = nullptr;
	QSpinBox*       m_timeoutSpin = nullptr;
	QPlainTextEdit* m_promptEdit = nullptr;
	QLabel*         m_pathLabel = nullptr;
	QLabel*         m_testResultLabel = nullptr;
	QPushButton*    m_testButton = nullptr;
};
