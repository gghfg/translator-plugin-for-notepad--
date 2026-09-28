#pragma once

// 与 ndd 主程序交互的辅助层：识别当前编辑器、读选区、算按钮位置、写回文本。
//
// 背景（插件最容易踩的坑）：
//   ndd 的 hex 视图 ScintillaHexEditView 和普通文本视图 ScintillaEditView
//   都继承自 QsciScintilla，而 getCurEdit() 返回的是 QsciScintilla*。
//   如果在 hex 模式下把二进制内容的"文本"送去翻译，再写回去，文件就毁了。
//   所以每次操作前必须判断当前视图是不是普通文本模式。
//
//   ndd 在编辑器控件上挂了动态属性，插件可以直接读，无需依赖主程序私有头文件：
//     "type"     -> NddDocType（1=普通文本 2=大文本只读 3=大文本读写 4=超大文本只读 5=hex）
//     "code"     -> 当前文档编码 id
//     "filePath" -> 当前文档路径

#include <QPoint>
#include <QString>

class QsciScintilla;

namespace NddHost
{

extern const char* const kPropDocType;
extern const char* const kPropCode;
extern const char* const kPropFilePath;

// 与上游 cceditor/ccnotepad.h 的 NddDocType 对应
enum DocType
{
	TxtType              = 1,
	BigTextReadOnly      = 2,
	BigTextReadWrite     = 3,
	SuperBigTextReadOnly = 4,
	HexType              = 5
};

enum class EditorKind
{
	None,     // 没有打开的编辑器
	Text,     // 普通文本，可读写，能安全翻译
	BigText,  // 大文本模式
	Hex,      // 十六进制模式
	Other     // 未知（拿不到属性，可能是新版 ndd 改了约定）
};

struct EditorInfo
{
	QsciScintilla* editor = nullptr;
	EditorKind     kind = EditorKind::None;
	QString        className; // 运行时真实类名，用于兜底判断
	QString        filePath;
	int            docType = 0;
	bool           readOnly = false;

	// 是否可以对它做翻译与回写
	bool translatable = false;
};

// 识别当前编辑器。editor 为空时返回 kind = None。
EditorInfo inspect(QsciScintilla* editor);

// 给用户看的类型说明，用于提示信息。
QString describeKind(EditorKind kind);

// ---- 选区 ----

QString selectedText(QsciScintilla* editor);

// 选区的字节长度（不复制文本，划选过程中频繁调用也便宜）
long selectionLength(QsciScintilla* editor);

// 选区末尾在编辑器控件内的坐标（相对编辑器左上角），用于把按钮贴到选区旁边。
// lineHeightOut 返回该行行高（拿不到时为 0）。返回 false 表示位置不可用。
bool selectionEndInEditor(QsciScintilla* editor, QPoint* pointOut, int* lineHeightOut);

// 文本里是否含有字母（纯符号/纯数字的选中内容不值得送去翻译）
bool containsLetters(const QString& text);

// ---- 写回 ----

// 用译文替换编辑器当前选区，包在一次 undo 事务里（一次 Ctrl+Z 可整体撤销）
bool replaceSelection(QsciScintilla* editor, const QString& text);

} // namespace NddHost
