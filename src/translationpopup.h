#pragma once

// 显示译文的浮窗。
//
// 用 Qt::Popup 而不是普通窗口：点界面别处、或按 Esc 会自动关闭，
// 不需要自己去装全局事件过滤器判断"点到外面了"。
// 窗口是无边框的，四角贴屏幕边缘时会被自动收进可用区域。

#include <QString>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

class TranslationPopup : public QWidget
{
	Q_OBJECT

public:
	explicit TranslationPopup(QWidget* parent = nullptr);
	~TranslationPopup() override;

	void showLoading();
	void showResult(const QString& translated);
	void showError(const QString& error);

	// 在锚点附近弹出（锚点通常是选区末尾的屏幕坐标）。
	// 默认放在锚点上方，越界时自动翻转/收拢。
	void popupNear(const QPoint& globalAnchor);

signals:
	// 用户点了"替换选区"。由 SelectionAssistant 负责校验选区没变再真正写回。
	void replaceRequested(const QString& translated);

protected:
	void keyPressEvent(QKeyEvent* event) override;

private slots:
	void onCopyClicked();
	void onReplaceClicked();

private:
	QWidget* buildUi();
	void     setBody(const QString& text, bool isError);
	void     applyHeightForText(const QString& text);
	void     moveNearAnchor(const QPoint& globalAnchor);

	QLabel*         m_title = nullptr;
	QPlainTextEdit* m_body = nullptr;
	QPushButton*    m_copyButton = nullptr;
	QPushButton*    m_replaceButton = nullptr;
	QLabel*         m_hint = nullptr;

	// 当前展示的译文，供"复制"和"替换"使用
	QString m_translated;
};
