#include "translationpopup.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QVBoxLayout>

namespace {

const int kPopupWidth = 460;

const char* const kOkStyle    = "color: #333333;";
const char* const kErrorStyle = "color: #c0392b;";

} // namespace

TranslationPopup::TranslationPopup(QWidget* parent)
	: QWidget(parent)
{
	// Qt::Popup：点到别处或按 Esc 自动关闭，省掉自己判定"点到外面了"
	setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
	setObjectName(QStringLiteral("nddTranslationPopup"));

	// 纯 QWidget 想用样式表画背景，必须打开这个属性
	setAttribute(Qt::WA_StyledBackground, true);
	setStyleSheet(QStringLiteral(
		"#nddTranslationPopup {"
		"  background-color: #fbfbfb;"
		"  border: 1px solid #9a9a9a;"
		"  border-radius: 4px;"
		"}"));

	setFixedWidth(kPopupWidth);

	QVBoxLayout* root = new QVBoxLayout(this);
	// buildUi() 返回的页面自己已经带了内边距，这里不要再叠一层
	root->setContentsMargins(0, 0, 0, 0);
	root->addWidget(buildUi());
}

TranslationPopup::~TranslationPopup() = default;

QWidget* TranslationPopup::buildUi()
{
	QWidget* page = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(page);
	layout->setContentsMargins(10, 8, 10, 8);

	m_title = new QLabel(QStringLiteral("译文"), page);
	m_title->setStyleSheet(QStringLiteral("font-weight: bold; color: #1a5fa8;"));
	layout->addWidget(m_title);

	m_body = new QPlainTextEdit(page);
	m_body->setReadOnly(true);
	m_body->setPlaceholderText(QStringLiteral("翻译结果会显示在这里"));
	layout->addWidget(m_body, 1);

	QHBoxLayout* buttonRow = new QHBoxLayout();

	m_hint = new QLabel(QStringLiteral("Esc 关闭"), page);
	m_hint->setStyleSheet(QStringLiteral("color: #999999;"));
	buttonRow->addWidget(m_hint);
	buttonRow->addStretch(1);

	m_replaceButton = new QPushButton(QStringLiteral("替换选区"), page);
	m_replaceButton->setToolTip(QStringLiteral("用译文替换编辑器里选中的文本（可 Ctrl+Z 撤销）"));
	connect(m_replaceButton, &QPushButton::clicked, this, &TranslationPopup::onReplaceClicked);
	buttonRow->addWidget(m_replaceButton);

	m_copyButton = new QPushButton(QStringLiteral("复制"), page);
	m_copyButton->setToolTip(QStringLiteral("把译文复制到剪贴板"));
	connect(m_copyButton, &QPushButton::clicked, this, &TranslationPopup::onCopyClicked);
	buttonRow->addWidget(m_copyButton);

	layout->addLayout(buttonRow);

	return page;
}

void TranslationPopup::setBody(const QString& text, bool isError)
{
	m_body->setStyleSheet(QString::fromLatin1(isError ? kErrorStyle : kOkStyle));
	m_body->setPlainText(text);
}

void TranslationPopup::showLoading()
{
	m_translated.clear();

	m_title->setText(QStringLiteral("正在翻译…"));
	setBody(QStringLiteral("请稍候…"), false);

	m_copyButton->setEnabled(false);
	m_replaceButton->setEnabled(false);

	setFixedHeight(140);
}

void TranslationPopup::showResult(const QString& translated)
{
	m_translated = translated;

	m_title->setText(QStringLiteral("译文"));
	setBody(translated, false);

	m_copyButton->setEnabled(!translated.isEmpty());
	m_replaceButton->setEnabled(!translated.isEmpty());

	applyHeightForText(translated);
}

void TranslationPopup::showError(const QString& error)
{
	m_translated.clear();

	m_title->setText(QStringLiteral("翻译失败"));
	setBody(error, true);

	m_copyButton->setEnabled(false);
	m_replaceButton->setEnabled(false);

	applyHeightForText(error);
}

void TranslationPopup::applyHeightForText(const QString& text)
{
	// 粗略估算换行后有几行：每行按 40 个字符算，再叠加显式换行。
	// 目标是让浮窗高度贴合内容，短译文不要留一大片空白。
	int approxLines = 1 + static_cast<int>(text.size() / 40);
	approxLines += text.count(QLatin1Char('\n'));
	if (approxLines < 1)
	{
		approxLines = 1;
	}

	const int bodyHeight = approxLines * 20 + 16;
	const int total = bodyHeight + 78; // 标题行 + 按钮行 + 边距

	setFixedHeight(qBound(120, total, 380));
}

void TranslationPopup::popupNear(const QPoint& globalAnchor)
{
	moveNearAnchor(globalAnchor);
	show();
	raise();
	activateWindow();
}

void TranslationPopup::moveNearAnchor(const QPoint& globalAnchor)
{
	const QSize popupSize = size();

	int x = globalAnchor.x();
	int y = globalAnchor.y() - popupSize.height() - 4; // 默认放到锚点上方

	QScreen* screen = QGuiApplication::screenAt(globalAnchor);
	if (screen == nullptr)
	{
		screen = QGuiApplication::primaryScreen();
	}

	if (screen != nullptr)
	{
		const QRect avail = screen->availableGeometry();
		const int left   = avail.x();
		const int top    = avail.y();
		const int right  = avail.x() + avail.width();
		const int bottom = avail.y() + avail.height();

		// 上方放不下就翻到锚点下方
		if (y < top)
		{
			y = globalAnchor.y() + 20;
		}

		if (y + popupSize.height() > bottom)
		{
			y = bottom - popupSize.height();
		}
		if (x + popupSize.width() > right)
		{
			x = right - popupSize.width();
		}
		if (x < left)
		{
			x = left;
		}
		if (y < top)
		{
			y = top;
		}
	}

	move(x, y);
}

void TranslationPopup::keyPressEvent(QKeyEvent* event)
{
	if (event != nullptr && event->key() == Qt::Key_Escape)
	{
		hide();
		event->accept();
		return;
	}

	QWidget::keyPressEvent(event);
}

void TranslationPopup::onCopyClicked()
{
	if (m_translated.isEmpty())
	{
		return;
	}

	if (QClipboard* clipboard = QApplication::clipboard())
	{
		clipboard->setText(m_translated);
	}
}

void TranslationPopup::onReplaceClicked()
{
	if (m_translated.isEmpty())
	{
		return;
	}

	emit replaceRequested(m_translated);
}
