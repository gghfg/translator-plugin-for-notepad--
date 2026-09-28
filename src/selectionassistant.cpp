#include "selectionassistant.h"

#include <QDialog>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <utility>

#include <qsciscintilla.h>

#include "deepseekclient.h"
#include "nddhost.h"
#include "settingsdialog.h"
#include "translationpopup.h"

namespace {

// 轮询当前编辑器的间隔。太短白费 CPU，太长切标签页后按钮出现得慢。
const int kPollIntervalMs = 350;

const int kButtonSize   = 28;
const int kCornerMargin = 20;

} // namespace

SelectionAssistant::SelectionAssistant(std::function<QsciScintilla*()> getCurEdit, QObject* parent)
	: QObject(parent)
	, m_getCurEdit(std::move(getCurEdit))
{
	// 宿主传进来的 parent 就是 ndd 的主窗口。留一份指针，用于"宿主回调返回空"时
	// 自己到窗口里找编辑器（见 resolveEditor）。
	m_hostWindow = qobject_cast<QWidget*>(parent);

	m_config.load();

	m_client = new DeepSeekClient(this);
	m_client->setConfig(m_config);

	// 顶层浮窗没有父对象，由本类负责释放（见析构函数）
	m_popup = new TranslationPopup(nullptr);
	connect(m_popup, &TranslationPopup::replaceRequested,
			this, &SelectionAssistant::onReplaceRequested);

	connect(m_client, &DeepSeekClient::finished, this, &SelectionAssistant::onTranslateFinished);
	connect(m_client, &DeepSeekClient::failed, this, &SelectionAssistant::onTranslateFailed);

	m_poll = new QTimer(this);
	m_poll->setInterval(kPollIntervalMs);
	connect(m_poll, &QTimer::timeout, this, &SelectionAssistant::pollCurrentEditor);
	m_poll->start();

	pollCurrentEditor();
}

SelectionAssistant::~SelectionAssistant()
{
	// m_popup 是顶层窗口，没有父对象，必须自己删
	delete m_popup;
	m_popup = nullptr;
}

// ------------------------------------------------------------------ 编辑器跟踪

void SelectionAssistant::pollCurrentEditor()
{
	QsciScintilla* editor = resolveEditor();

	if (editor != m_editor.data())
	{
		attachToEditor(editor);
		return;
	}

	// 编辑器没换，但窗口可能被拖动或缩放过，按钮位置要跟上。
	// 只在按钮可见时重摆，避免反复 show/raise 造成闪烁。
	if (editor != nullptr && !m_button.isNull() && m_button->isVisible())
	{
		positionButton();
	}
}

QsciScintilla* SelectionAssistant::resolveEditor()
{
	// 首选宿主给的"当前编辑器"回调
	if (m_getCurEdit)
	{
		if (QsciScintilla* editor = m_getCurEdit())
		{
			return editor;
		}
	}

	// 兜底：这个回调在不同 ndd 版本 / 多窗口 / 视图类型下都可能返回空，
	// 一旦返回空插件就彻底瘫痪（按钮不出现、菜单报"没有打开的编辑器"）。
	// 所以这里自己在宿主窗口里找：
	//   findChildren 走 Qt 元对象系统，跨 DLL 也成立；
	//   ndd 的编辑器 ScintillaEditView 继承自 QsciScintilla，能被匹配到。
	// 再按 ndd 挂在控件上的 type 属性过滤掉 hex / 大文本视图。
	if (m_hostWindow == nullptr)
	{
		return nullptr;
	}

	const QList<QsciScintilla*> candidates = m_hostWindow->findChildren<QsciScintilla*>();
	QsciScintilla* fallback = nullptr;

	for (QsciScintilla* candidate : candidates)
	{
		if (candidate == nullptr)
		{
			continue;
		}

		if (!NddHost::inspect(candidate).translatable)
		{
			continue;
		}

		// 当前标签页的控件是可见的；非当前标签页会被 QTabWidget hide 掉
		if (candidate->isVisible())
		{
			return candidate;
		}

		if (fallback == nullptr && !candidate->isHidden())
		{
			fallback = candidate;
		}
	}

	return fallback;
}

void SelectionAssistant::rebindHost(QWidget* host, std::function<QsciScintilla*()> getCurEdit)
{
	m_hostWindow = host;

	if (getCurEdit)
	{
		m_getCurEdit = std::move(getCurEdit);
	}

	// 立刻重新解析一次，让按钮跟到当前这个窗口
	pollCurrentEditor();
}

void SelectionAssistant::attachToEditor(QsciScintilla* editor)
{
	QsciScintilla* previous = m_editor.data();
	if (previous != nullptr)
	{
		disconnect(previous, &QsciScintilla::selectionChanged,
				   this, &SelectionAssistant::onEditorSelectionChanged);
	}

	m_editor = editor;

	if (!m_button.isNull())
	{
		m_button->hide();
	}

	if (editor == nullptr)
	{
		return;
	}

	ensureButton(editor);

	connect(editor, &QsciScintilla::selectionChanged,
			this, &SelectionAssistant::onEditorSelectionChanged);

	updateButton();
}

void SelectionAssistant::ensureButton(QWidget* parent)
{
	if (m_button.isNull())
	{
		QToolButton* button = new QToolButton(parent);
		button->setText(QStringLiteral("译"));
		button->setToolTip(QStringLiteral("翻译选中的文本"));
		button->setFixedSize(kButtonSize, kButtonSize);

		// 关键：不抢焦点。否则点按钮时编辑器失焦，选区虽然还在，
		// 但用户看到的是灰掉的选区，体验很差。
		button->setFocusPolicy(Qt::NoFocus);
		button->setCursor(Qt::PointingHandCursor);

		button->setStyleSheet(QStringLiteral(
			"QToolButton {"
			"  border: 1px solid #4a90d9;"
			"  border-radius: 4px;"
			"  background-color: #eaf3fd;"
			"  color: #1a5fa8;"
			"  font-size: 13px;"
			"}"
			"QToolButton:hover { background-color: #d3e7fb; }"));

		m_button = button;
		connect(button, &QToolButton::clicked, this, &SelectionAssistant::onButtonClicked);
	}
	else if (m_button->parentWidget() != parent)
	{
		// 换标签页时把按钮挪到新的编辑器上。
		// 做成编辑器的子控件有个好处：切标签/关标签时按钮会自动跟着隐藏或销毁，
		// 不用自己去同步可见性。
		m_button->setParent(parent);
	}

	m_button->hide();
}

void SelectionAssistant::onEditorSelectionChanged()
{
	updateButton();
}

void SelectionAssistant::updateButton()
{
	QsciScintilla* editor = m_editor.data();

	if (editor == nullptr || m_button.isNull())
	{
		if (!m_button.isNull())
		{
			m_button->hide();
		}
		return;
	}

	// hex / 大文本 / 只读模式下不提供翻译，干脆不显示按钮，
	// 免得用户点了才被告知不能用。
	const NddHost::EditorInfo info = NddHost::inspect(editor);
	if (!info.translatable || NddHost::selectionLength(editor) <= 0)
	{
		m_button->hide();
		return;
	}

	positionButton();
	m_button->show();
	m_button->raise();
}

void SelectionAssistant::positionButton()
{
	QsciScintilla* editor = m_editor.data();
	if (editor == nullptr || m_button.isNull())
	{
		return;
	}

	const QSize buttonSize = m_button->size();

	if (m_config.buttonPlacement == TranslatorConfig::ButtonPlacement::SelectionEnd)
	{
		QPoint endInEditor;
		int lineHeight = 0;

		if (NddHost::selectionEndInEditor(editor, &endInEditor, &lineHeight))
		{
			// 贴在选区末尾的右下方一点
			const int dy = (lineHeight > 0) ? lineHeight : 18;

			int x = endInEditor.x() + 6;
			int y = endInEditor.y() + dy;

			x = qBound(0, x, qMax(0, editor->width() - buttonSize.width()));
			y = qBound(0, y, qMax(0, editor->height() - buttonSize.height()));

			m_button->move(x, y);
			return;
		}
		// 取不到位置就退回到角落方案
	}

	// 编辑器右下角
	const int x = qMax(0, editor->width() - buttonSize.width() - kCornerMargin);
	const int y = qMax(0, editor->height() - buttonSize.height() - kCornerMargin);

	m_button->move(x, y);
}

QPoint SelectionAssistant::anchorGlobalPos() const
{
	QsciScintilla* editor = m_editor.data();
	if (editor == nullptr)
	{
		return QPoint(400, 300);
	}

	QPoint endInEditor;
	int lineHeight = 0;

	if (NddHost::selectionEndInEditor(editor, &endInEditor, &lineHeight))
	{
		const int dy = (lineHeight > 0) ? lineHeight : 18;
		return editor->mapToGlobal(QPoint(endInEditor.x(), endInEditor.y() + dy));
	}

	// 退回到编辑器右下角
	return editor->mapToGlobal(QPoint(editor->width() - 40, editor->height() - 40));
}

// ------------------------------------------------------------------ 翻译

void SelectionAssistant::onButtonClicked()
{
	startTranslation();
}

void SelectionAssistant::translateSelectionNow()
{
	startTranslation();
}

void SelectionAssistant::startTranslation()
{
	// 先让编辑器跟踪跑一遍：从菜单点进来时，宿主回调可能刚好可用而轮询还没轮到
	pollCurrentEditor();

	QsciScintilla* editor = m_editor.data();
	if (editor == nullptr)
	{
		reportProblem(QStringLiteral(
			"没有找到可用的编辑器。\n\n"
			"可能原因：当前标签页不是普通文本模式（hex / 大文本只读都不支持），"
			"或者助手绑定的不是你正在用的这个 ndd 窗口。\n\n"
			"菜单里的「诊断」会列出插件实际看到的编辑器情况。"));
		return;
	}

	const NddHost::EditorInfo info = NddHost::inspect(editor);
	if (!info.translatable)
	{
		const QString reason =
			info.readOnly ? QStringLiteral("文档是只读的")
						  : QStringLiteral("当前是%1").arg(NddHost::describeKind(info.kind));
		reportProblem(QStringLiteral("%1，只能翻译普通文本。").arg(reason));
		return;
	}

	const QString text = NddHost::selectedText(editor);

	if (text.trimmed().isEmpty())
	{
		if (!m_button.isNull())
		{
			m_button->hide();
		}
		reportProblem(QStringLiteral("请先在编辑器里选中要翻译的文本。"));
		return;
	}

	if (text.size() > TranslatorConfig::maxSelectionChars())
	{
		reportProblem(QStringLiteral("选中的内容太长了（%1 个字符，上限 %2）。请只选中要翻译的那一段。")
						  .arg(text.size())
						  .arg(TranslatorConfig::maxSelectionChars()));
		return;
	}

	if (!NddHost::containsLetters(text))
	{
		reportProblem(QStringLiteral("选中的内容里没有可翻译的文字（只有符号或数字）。"));
		return;
	}

	if (m_config.apiKey.trimmed().isEmpty())
	{
		reportProblem(QStringLiteral("尚未配置 API Key，请先在“设置”里填写。"));
		openSettings();
		return;
	}

	// 锁定这次要翻译的内容，替换前会拿它比对，确认选区没被改动
	m_pendingSelection = text;

	// 锚点要在隐藏按钮之前算（虽然用的是选区位置，但保持顺序清晰）
	const QPoint anchor = anchorGlobalPos();

	if (!m_button.isNull())
	{
		m_button->hide();
	}

	m_popup->showLoading();
	m_popup->popupNear(anchor);

	m_client->setConfig(m_config);
	m_client->translate(text);
}

void SelectionAssistant::onTranslateFinished(const QString& translated)
{
	m_popup->showResult(translated);
}

void SelectionAssistant::onTranslateFailed(const QString& error)
{
	m_popup->showError(error);
}

void SelectionAssistant::onReplaceRequested(const QString& translated)
{
	QsciScintilla* editor = m_editor.data();
	if (editor == nullptr)
	{
		m_popup->showError(QStringLiteral("编辑器已经关闭，无法替换。"));
		return;
	}

	// 选区在翻译期间被改动过就拒绝，避免把译文写到错误的位置
	if (NddHost::selectedText(editor) != m_pendingSelection)
	{
		m_popup->showError(
			QStringLiteral("编辑器里的选区已经变了。为避免替换错位置，请重新选中再翻译一次。"));
		return;
	}

	if (NddHost::replaceSelection(editor, translated))
	{
		m_pendingSelection.clear();
		m_popup->hide();
	}
	else
	{
		m_popup->showError(QStringLiteral("替换失败：文档可能是只读的。"));
	}
}

void SelectionAssistant::reportProblem(const QString& message)
{
	m_popup->showError(message);
	m_popup->popupNear(anchorGlobalPos());
}

// ------------------------------------------------------------------ 设置

void SelectionAssistant::openSettings()
{
	SettingsDialog dialog(m_config, nullptr);
	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	m_config = dialog.config();

	// 保存失败不阻止本次使用，只是下次启动读不到新值
	m_config.save();

	m_client->setConfig(m_config);

	// 放置方式可能改了，重新摆一下按钮
	updateButton();
}

void SelectionAssistant::reloadConfig()
{
	m_config.load();
	m_client->setConfig(m_config);
	updateButton();
}

// ------------------------------------------------------------------ 诊断

QString SelectionAssistant::diagnosticText() const
{
	QStringList lines;

	lines << QStringLiteral("【宿主绑定】");
	lines << QStringLiteral("主窗口：%1").arg(
		m_hostWindow != nullptr ? QString::fromLatin1(m_hostWindow->metaObject()->className())
								: QStringLiteral("（未绑定）"));

	QsciScintilla* fromHost = nullptr;
	if (m_getCurEdit)
	{
		fromHost = m_getCurEdit();
	}
	lines << QStringLiteral("getCurEdit() 返回：%1").arg(
		fromHost != nullptr ? QStringLiteral("有效指针") : QStringLiteral("nullptr"));

	QWidget* parentWidget = (fromHost != nullptr) ? fromHost->parentWidget() : nullptr;
	lines << QStringLiteral("该编辑器的父控件：%1").arg(
		parentWidget != nullptr
			? QString::fromLatin1(parentWidget->metaObject()->className())
			: QStringLiteral("（无）"));

	lines << QString();
	lines << QStringLiteral("【助手当前跟踪的编辑器】");
	QsciScintilla* editor = m_editor.data();
	if (editor == nullptr)
	{
		lines << QStringLiteral("（无）");
	}
	else
	{
		const NddHost::EditorInfo info = NddHost::inspect(editor);
		lines << QStringLiteral("运行时类名：%1").arg(info.className);
		lines << QStringLiteral("type 属性：%1").arg(info.docType);
		lines << QStringLiteral("模式判定：%1").arg(NddHost::describeKind(info.kind));
		lines << QStringLiteral("只读：%1").arg(info.readOnly ? QStringLiteral("是") : QStringLiteral("否"));
		lines << QStringLiteral("选区长度：%1").arg(NddHost::selectionLength(editor));
		lines << QStringLiteral("可否翻译：%1").arg(info.translatable ? QStringLiteral("可以") : QStringLiteral("不可以"));
		lines << QStringLiteral("控件可见：%1").arg(editor->isVisible() ? QStringLiteral("是") : QStringLiteral("否"));
	}

	lines << QString();
	lines << QStringLiteral("【宿主窗口里能搜到的编辑器】");
	if (m_hostWindow == nullptr)
	{
		lines << QStringLiteral("（没拿到主窗口指针，无法搜索）");
	}
	else
	{
		const QList<QsciScintilla*> all = m_hostWindow->findChildren<QsciScintilla*>();
		lines << QStringLiteral("共 %1 个").arg(all.size());

		int index = 0;
		for (QsciScintilla* one : all)
		{
			if (one == nullptr)
			{
				continue;
			}

			const NddHost::EditorInfo info = NddHost::inspect(one);
			lines << QStringLiteral("  [%1] %2  type=%3  %4  可见=%5  选区=%6")
						 .arg(index++)
						 .arg(info.className)
						 .arg(info.docType)
						 .arg(NddHost::describeKind(info.kind))
						 .arg(one->isVisible() ? QStringLiteral("是") : QStringLiteral("否"))
						 .arg(NddHost::selectionLength(one));
		}
	}

	lines << QString();
	lines << QStringLiteral("【「译」按钮】");
	if (m_button.isNull())
	{
		lines << QStringLiteral("不存在（从未创建）");
	}
	else
	{
		lines << QStringLiteral("存在，可见=%1，位置=(%2,%3)，尺寸=%4x%5")
					 .arg(m_button->isVisible() ? QStringLiteral("是") : QStringLiteral("否"))
					 .arg(m_button->x())
					 .arg(m_button->y())
					 .arg(m_button->width())
					 .arg(m_button->height());
	}

	lines << QString();
	lines << QStringLiteral("【配置】");
	lines << QStringLiteral("配置文件：%1").arg(TranslatorConfig::filePath());
	lines << QStringLiteral("API Key：%1").arg(
		m_config.apiKey.isEmpty() ? QStringLiteral("未填写") : QStringLiteral("已填写"));
	lines << QStringLiteral("接口地址：%1").arg(m_config.chatCompletionsUrl());
	lines << QStringLiteral("按钮位置：%1").arg(
		m_config.buttonPlacement == TranslatorConfig::ButtonPlacement::SelectionEnd
			? QStringLiteral("贴着选区末尾")
			: QStringLiteral("编辑器右下角"));

	lines << QString();
	lines << QStringLiteral("【传输层】");
	lines << DeepSeekClient::transportInfo();

	return lines.join(QStringLiteral("\n"));
}

void SelectionAssistant::showDiagnostics()
{
	// 先刷新一次，让报告反映最新状态
	pollCurrentEditor();

	QMessageBox box(QMessageBox::Information,
					QStringLiteral("DeepSeek 翻译 · 诊断"),
					diagnosticText(),
					QMessageBox::Ok);
	box.exec();
}
