#pragma once

// 选词助手：本插件唯一的核心逻辑。
//
// 行为：在编辑器里选中一段文本 → 编辑器角落出现一个「译」小按钮 →
//       点它 → 在选区旁边浮窗显示译文。
//
// 编辑器跟踪：ndd 没有"当前标签页切换"的回调，插件只在菜单被点击时被通知，
// 所以这里用 QTimer 轮询 getCurEdit() 感知编辑器切换，切换后重新挂接
// QsciScintilla::selectionChanged 信号。
//
// 关于"按钮不抢焦点"：按钮是编辑器的子控件，并且设成 Qt::NoFocus。
// 这样点按钮时编辑器不会被夺走焦点，Scintilla 的选区依然保留，
// 我们才能在点击那一刻读到用户选中的文本。

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <functional>

#include "translatorconfig.h"

class QTimer;
class QToolButton;
class QWidget;
class QsciScintilla;

class DeepSeekClient;
class TranslationPopup;

class SelectionAssistant : public QObject
{
	Q_OBJECT

public:
	explicit SelectionAssistant(std::function<QsciScintilla*()> getCurEdit,
								QObject* parent = nullptr);
	~SelectionAssistant() override;

	// 供宿主菜单调用
	void openSettings();
	void reloadConfig();
	void translateSelectionNow();

	// 宿主窗口重绑。
	//
	// ndd 每开一个新窗口都会再调一次 NDD_PROC_MAIN，那时必须把助手重新绑到
	// 新窗口和新的"取当前编辑器"回调上；否则插件会一直盯着第一个窗口，
	// 在别的窗口里就会表现为"没有打开的编辑器"、按钮也不出现。
	void rebindHost(QWidget* host, std::function<QsciScintilla*()> getCurEdit);

	// 把当前内部状态显示出来。排查"按钮不出现 / 没有打开的编辑器"用。
	void showDiagnostics();

private slots:
	void pollCurrentEditor();
	void onEditorSelectionChanged();
	void onButtonClicked();
	void onTranslateFinished(const QString& translated);
	void onTranslateFailed(const QString& error);
	void onReplaceRequested(const QString& translated);

private:
	void    attachToEditor(QsciScintilla* editor);
	void    ensureButton(QWidget* parent);
	void    updateButton();
	void    positionButton();
	QPoint  anchorGlobalPos() const;
	void    startTranslation();
	void    reportProblem(const QString& message);

	// 取当前该操作的编辑器。优先用宿主给的 getCurEdit()；它返回空时，
	// 退化为在宿主窗口里自己找（见 .cpp 里的说明）。
	QsciScintilla* resolveEditor();

	// 诊断信息（纯文本）
	QString diagnosticText() const;

	std::function<QsciScintilla*()> m_getCurEdit;
	QWidget*          m_hostWindow = nullptr;
	DeepSeekClient*   m_client = nullptr;
	TranslationPopup* m_popup = nullptr;
	TranslatorConfig  m_config;

	QPointer<QsciScintilla> m_editor;
	QPointer<QToolButton>   m_button;
	QTimer*                 m_poll = nullptr;

	// 点击按钮那一刻锁定的选区内容；替换前用来确认选区没被改动过
	QString m_pendingSelection;
};
