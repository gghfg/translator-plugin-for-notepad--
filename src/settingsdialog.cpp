#include "settingsdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "deepseekclient.h"

namespace {

QStringList languageOptions()
{
	QStringList list;
	list << QStringLiteral("自动检测")
		 << QStringLiteral("中文")
		 << QStringLiteral("英语")
		 << QStringLiteral("日语")
		 << QStringLiteral("韩语")
		 << QStringLiteral("法语")
		 << QStringLiteral("德语")
		 << QStringLiteral("俄语")
		 << QStringLiteral("西班牙语")
		 << QStringLiteral("葡萄牙语")
		 << QStringLiteral("意大利语")
		 << QStringLiteral("阿拉伯语");
	return list;
}

QStringList modelOptions()
{
	QStringList list;
	list << QStringLiteral("deepseek-chat") << QStringLiteral("deepseek-reasoner");
	return list;
}

// 保证下拉框里一定含有当前值，否则用户手改 ini 后这里会莫名跳回第一项
void selectOrAdd(QComboBox* combo, const QString& value)
{
	if (combo == nullptr || value.isEmpty())
	{
		return;
	}

	int index = combo->findText(value);
	if (index < 0)
	{
		combo->addItem(value);
		index = combo->findText(value);
	}

	combo->setCurrentIndex(index);
}

const int kPlacementCornerIndex    = 0;
const int kPlacementSelectionIndex = 1;

} // namespace

SettingsDialog::SettingsDialog(const TranslatorConfig& config, QWidget* parent)
	: QDialog(parent)
	, m_config(config)
{
	setWindowTitle(QStringLiteral("DeepSeek 翻译 · 设置"));
	setMinimumWidth(540);

	m_testClient = new DeepSeekClient(this);
	connect(m_testClient, &DeepSeekClient::finished, this, &SettingsDialog::onTestFinished);
	connect(m_testClient, &DeepSeekClient::failed, this, &SettingsDialog::onTestFailed);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->addWidget(buildForm());

	m_testResultLabel = new QLabel(this);
	m_testResultLabel->setWordWrap(true);
	m_testResultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	root->addWidget(m_testResultLabel);

	QDialogButtonBox* box = new QDialogButtonBox(this);

	QPushButton* resetButton = new QPushButton(QStringLiteral("恢复默认"), this);
	connect(resetButton, &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);
	box->addButton(resetButton, QDialogButtonBox::ResetRole);

	m_testButton = new QPushButton(QStringLiteral("测试连接"), this);
	connect(m_testButton, &QPushButton::clicked, this, &SettingsDialog::testConnection);
	box->addButton(m_testButton, QDialogButtonBox::ActionRole);

	QPushButton* saveButton = new QPushButton(QStringLiteral("保存"), this);
	saveButton->setDefault(true);
	box->addButton(saveButton, QDialogButtonBox::AcceptRole);

	QPushButton* cancelButton = new QPushButton(QStringLiteral("取消"), this);
	box->addButton(cancelButton, QDialogButtonBox::RejectRole);

	connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

	root->addWidget(box);

	applyConfigToUi();
}

SettingsDialog::~SettingsDialog() = default;

QWidget* SettingsDialog::buildForm()
{
	QWidget* page = new QWidget(this);
	QFormLayout* form = new QFormLayout(page);
	form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

	// ---- API Key：默认打码，可以临时显示
	QWidget* keyRow = new QWidget(page);
	QHBoxLayout* keyLayout = new QHBoxLayout(keyRow);
	keyLayout->setContentsMargins(0, 0, 0, 0);

	m_keyEdit = new QLineEdit(keyRow);
	m_keyEdit->setEchoMode(QLineEdit::Password);
	m_keyEdit->setPlaceholderText(QStringLiteral("sk-..."));
	keyLayout->addWidget(m_keyEdit, 1);

	m_keyToggle = new QPushButton(QStringLiteral("显示"), keyRow);
	m_keyToggle->setCheckable(true);
	m_keyToggle->setFixedWidth(56);
	connect(m_keyToggle, &QPushButton::clicked, this, &SettingsDialog::toggleKeyVisible);
	keyLayout->addWidget(m_keyToggle);

	form->addRow(QStringLiteral("API Key"), keyRow);

	m_baseUrlEdit = new QLineEdit(page);
	m_baseUrlEdit->setPlaceholderText(QString::fromLatin1(TranslatorConfig::kDefaultBaseUrl));
	form->addRow(QStringLiteral("接口地址"), m_baseUrlEdit);

	m_modelCombo = new QComboBox(page);
	m_modelCombo->setEditable(true);
	m_modelCombo->addItems(modelOptions());
	form->addRow(QStringLiteral("模型"), m_modelCombo);

	m_temperatureSpin = new QDoubleSpinBox(page);
	m_temperatureSpin->setRange(0.0, 2.0);
	m_temperatureSpin->setSingleStep(0.1);
	m_temperatureSpin->setDecimals(1);
	form->addRow(QStringLiteral("温度"), m_temperatureSpin);

	m_sourceCombo = new QComboBox(page);
	m_sourceCombo->setEditable(true);
	m_sourceCombo->addItems(languageOptions());
	form->addRow(QStringLiteral("源语言"), m_sourceCombo);

	m_targetCombo = new QComboBox(page);
	m_targetCombo->setEditable(true);
	m_targetCombo->addItems(languageOptions());
	form->addRow(QStringLiteral("目标语言"), m_targetCombo);

	// ---- 悬浮按钮
	QGroupBox* buttonGroup = new QGroupBox(QStringLiteral("悬浮按钮"), page);
	QFormLayout* buttonForm = new QFormLayout(buttonGroup);

	m_placementCombo = new QComboBox(buttonGroup);
	m_placementCombo->addItem(QStringLiteral("编辑器右下角"));
	m_placementCombo->addItem(QStringLiteral("贴着选区末尾"));
	buttonForm->addRow(QStringLiteral("出现位置"), m_placementCombo);

	form->addRow(buttonGroup);

	m_timeoutSpin = new QSpinBox(page);
	m_timeoutSpin->setRange(5, 300);
	m_timeoutSpin->setSuffix(QStringLiteral(" 秒"));
	form->addRow(QStringLiteral("请求超时"), m_timeoutSpin);

	// ---- 提示词
	m_promptEdit = new QPlainTextEdit(page);
	m_promptEdit->setMinimumHeight(120);
	form->addRow(QStringLiteral("系统提示词"), m_promptEdit);

	// ---- 存储位置说明
	QString storageHint = QStringLiteral("配置文件：%1").arg(TranslatorConfig::filePath());
#ifdef Q_OS_WIN
	storageHint += QStringLiteral("\nAPI Key 使用 Windows DPAPI 加密后存储（绑定当前用户账户）。");
#else
	storageHint += QStringLiteral("\n当前平台无 DPAPI，API Key 仅做 Base64 混淆，请注意保管。");
#endif

	m_pathLabel = new QLabel(storageHint, page);
	m_pathLabel->setWordWrap(true);
	m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_pathLabel->setStyleSheet(QStringLiteral("color: #666;"));
	form->addRow(m_pathLabel);

	return page;
}

void SettingsDialog::applyConfigToUi()
{
	m_keyEdit->setText(m_config.apiKey);
	m_baseUrlEdit->setText(m_config.baseUrl);
	selectOrAdd(m_modelCombo, m_config.model);
	m_temperatureSpin->setValue(m_config.temperature);
	selectOrAdd(m_sourceCombo, m_config.sourceLang);
	selectOrAdd(m_targetCombo, m_config.targetLang);

	m_placementCombo->setCurrentIndex(
		m_config.buttonPlacement == TranslatorConfig::ButtonPlacement::SelectionEnd
			? kPlacementSelectionIndex
			: kPlacementCornerIndex);

	m_timeoutSpin->setValue(qMax(5, m_config.timeoutMs / 1000));
	m_promptEdit->setPlainText(m_config.systemPrompt);
}

TranslatorConfig SettingsDialog::readUi() const
{
	TranslatorConfig cfg;

	cfg.apiKey = m_keyEdit->text().trimmed();
	cfg.baseUrl = m_baseUrlEdit->text().trimmed();
	cfg.model = m_modelCombo->currentText().trimmed();
	cfg.temperature = m_temperatureSpin->value();
	cfg.sourceLang = m_sourceCombo->currentText().trimmed();
	cfg.targetLang = m_targetCombo->currentText().trimmed();
	cfg.timeoutMs = m_timeoutSpin->value() * 1000;
	cfg.systemPrompt = m_promptEdit->toPlainText().trimmed();

	cfg.buttonPlacement = (m_placementCombo->currentIndex() == kPlacementSelectionIndex)
							  ? TranslatorConfig::ButtonPlacement::SelectionEnd
							  : TranslatorConfig::ButtonPlacement::EditorCorner;

	cfg.normalize();
	return cfg;
}

TranslatorConfig SettingsDialog::config() const
{
	return readUi();
}

void SettingsDialog::toggleKeyVisible()
{
	const bool visible = m_keyToggle->isChecked();
	m_keyEdit->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
	m_keyToggle->setText(visible ? QStringLiteral("隐藏") : QStringLiteral("显示"));
}

void SettingsDialog::restoreDefaults()
{
	TranslatorConfig defaults;
	// 只重置可调项，不动用户已经填好的 API Key
	defaults.apiKey = m_keyEdit->text().trimmed();

	m_config = defaults;
	applyConfigToUi();

	m_testResultLabel->clear();
}

void SettingsDialog::testConnection()
{
	const TranslatorConfig cfg = readUi();

	if (cfg.apiKey.isEmpty())
	{
		m_testResultLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
		m_testResultLabel->setText(QStringLiteral("请先填写 API Key。"));
		return;
	}

	setTestBusy(true);
	m_testResultLabel->setStyleSheet(QStringLiteral("color: #666;"));
	m_testResultLabel->setText(QStringLiteral("正在测试…"));

	m_testClient->setConfig(cfg);
	m_testClient->translate(QStringLiteral("Hello, world."));
}

void SettingsDialog::onTestFinished(const QString& translated)
{
	setTestBusy(false);
	m_testResultLabel->setStyleSheet(QStringLiteral("color: #1a7f37;"));
	m_testResultLabel->setText(
		QStringLiteral("连接成功。模型返回：%1").arg(translated.simplified()));
}

void SettingsDialog::onTestFailed(const QString& error)
{
	setTestBusy(false);
	m_testResultLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
	m_testResultLabel->setText(QStringLiteral("测试失败：%1").arg(error));
}

void SettingsDialog::setTestBusy(bool busy)
{
	m_testButton->setEnabled(!busy);
}
