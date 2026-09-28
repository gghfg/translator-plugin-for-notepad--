#include "nddhost.h"

#include <QChar>
#include <QMetaObject>
#include <QVariant>
#include <QWidget>

#include <qsciscintilla.h>

namespace NddHost
{

const char* const kPropDocType  = "type";
const char* const kPropCode     = "code";
const char* const kPropFilePath = "filePath";

EditorInfo inspect(QsciScintilla* editor)
{
	EditorInfo info;
	info.editor = editor;

	if (editor == nullptr)
	{
		return info;
	}

	const QMetaObject* meta = editor->metaObject();
	if (meta != nullptr)
	{
		info.className = QString::fromLatin1(meta->className());
	}

	const QVariant typeValue = editor->property(kPropDocType);
	if (typeValue.isValid())
	{
		info.docType = typeValue.toInt();
	}

	const QVariant pathValue = editor->property(kPropFilePath);
	if (pathValue.isValid())
	{
		info.filePath = pathValue.toString();
	}

	info.readOnly = editor->isReadOnly();

	// 优先信任 type 属性（这是主程序自己写的，最权威）；
	// 万一将来主程序改了属性名，再用运行时类名兜底。
	if (info.docType > 0)
	{
		switch (info.docType)
		{
		case TxtType:
			info.kind = EditorKind::Text;
			break;
		case HexType:
			info.kind = EditorKind::Hex;
			break;
		case BigTextReadOnly:
		case BigTextReadWrite:
		case SuperBigTextReadOnly:
			info.kind = EditorKind::BigText;
			break;
		default:
			info.kind = EditorKind::Other;
			break;
		}
	}
	else if (info.className == QStringLiteral("ScintillaHexEditView"))
	{
		info.kind = EditorKind::Hex;
	}
	else if (info.className == QStringLiteral("ScintillaEditView"))
	{
		info.kind = EditorKind::Text;
	}

	info.translatable = (info.kind == EditorKind::Text) && !info.readOnly;

	return info;
}

QString describeKind(EditorKind kind)
{
	switch (kind)
	{
	case EditorKind::Text:    return QStringLiteral("普通文本");
	case EditorKind::BigText: return QStringLiteral("大文本模式");
	case EditorKind::Hex:     return QStringLiteral("十六进制模式");
	case EditorKind::Other:   return QStringLiteral("未知模式");
	case EditorKind::None:
	default:                  return QStringLiteral("没有打开的编辑器");
	}
}

// ------------------------------------------------------------------ 选区

QString selectedText(QsciScintilla* editor)
{
	if (editor == nullptr)
	{
		return QString();
	}

	return editor->selectedText();
}

long selectionLength(QsciScintilla* editor)
{
	if (editor == nullptr)
	{
		return 0;
	}

	// SendScintilla 的重载里有带默认参数的
	//   long SendScintilla(unsigned int msg, unsigned long wParam = 0, long lParam = 0)
	// SCI_GETSELECTIONSTART/END 只用返回值，不关心这两个参数，所以单参调用即可，
	// 且因为只有这一个重载带默认参数，不会产生歧义。
	const long start = editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART);
	const long end   = editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND);

	if (end <= start)
	{
		return 0;
	}

	return end - start;
}

bool selectionEndInEditor(QsciScintilla* editor, QPoint* pointOut, int* lineHeightOut)
{
	if (pointOut != nullptr)
	{
		*pointOut = QPoint();
	}
	if (lineHeightOut != nullptr)
	{
		*lineHeightOut = 0;
	}

	if (editor == nullptr)
	{
		return false;
	}

	const long endPos = editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND);
	if (endPos < 0)
	{
		return false;
	}

	// SCI_POINTX/YFROMPOSITION 需要三个参数，用显式类型避免落到别的重载上
	const long x = editor->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION,
										 static_cast<unsigned long>(0),
										 static_cast<long>(endPos));
	const long y = editor->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION,
										 static_cast<unsigned long>(0),
										 static_cast<long>(endPos));

	if (x < 0 || y < 0)
	{
		return false;
	}

	// 这两个坐标是相对文本区（viewport）的，需要换算到编辑器控件坐标，
	// 因为悬浮按钮是编辑器的子控件，用控件坐标定位。
	const QPoint inViewport(static_cast<int>(x), static_cast<int>(y));
	QWidget* viewport = editor->viewport();
	const QPoint inEditor =
		(viewport != nullptr) ? viewport->mapTo(editor, inViewport) : inViewport;

	if (pointOut != nullptr)
	{
		*pointOut = inEditor;
	}

	if (lineHeightOut != nullptr)
	{
		const long line = editor->SendScintilla(
			QsciScintillaBase::SCI_LINEFROMPOSITION, static_cast<unsigned long>(endPos));

		const long height = editor->SendScintilla(
			QsciScintillaBase::SCI_TEXTHEIGHT, static_cast<unsigned long>(line));

		*lineHeightOut = (height > 0) ? static_cast<int>(height) : 0;
	}

	return true;
}

bool containsLetters(const QString& text)
{
	for (int i = 0; i < text.size(); ++i)
	{
		if (text.at(i).isLetter())
		{
			return true;
		}
	}

	return false;
}

// ------------------------------------------------------------------ 写回

bool replaceSelection(QsciScintilla* editor, const QString& text)
{
	if (editor == nullptr || editor->isReadOnly())
	{
		return false;
	}

	// 刻意用 selectionLength()（走导出的 SendScintilla）而不是 hasSelectedText()：
	// 后者是头文件里的内联函数，直接读受保护成员 selText，会依赖类布局与宿主 DLL
	// 里那份 QScintilla 完全一致。走导出函数则没有这个隐含依赖。
	if (selectionLength(editor) <= 0)
	{
		return false;
	}

	// 包进一次 undo 事务，用户按一次 Ctrl+Z 就能整体撤销
	editor->beginUndoAction();

	// 关键：这里用**限定名调用** editor->QsciScintilla::replaceSelectedText(...)，
	// 而不是普通的 editor->replaceSelectedText(...)。
	//
	// 原因是 replaceSelectedText 是虚函数：普通调用会走 vtable，而槽位下标是**编译期
	// 按头文件的虚函数声明顺序**算出来的。插件自带的 QScintilla 头文件与宿主 DLL
	// 未必是同一版本（本项目就实测到过：头里 87 个虚函数有 4 个在 DLL 里根本不存在，
	// vtable 布局因此错位）。一旦错位，虚调用就会跳到别的函数上，编译器与链接器都
	// 不会报错。写限定名可以抑制虚派发，直接引用导出的
	//   ?replaceSelectedText@QsciScintilla@@UEAAXAEBVQString@@@Z
	// 从而彻底不受 vtable 布局影响；万一签名真的对不上，也会在链接期就失败而不是运行期崩。
	editor->QsciScintilla::replaceSelectedText(text);

	editor->endUndoAction();

	return true;
}

} // namespace NddHost
