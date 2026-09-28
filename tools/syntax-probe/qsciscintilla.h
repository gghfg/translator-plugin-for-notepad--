// 语法自检专用的 QsciScintilla 假头文件。
//
// 为什么要有它：真实头文件在 third_party/include/Qsci/ 下，但它会拉进
// QsciLexer / QsciStyle / QColor / QFont / QImage ... 一大串类型，桩起来代价太高。
//
// 这里的声明是照 **真实头文件与 qmyedit_qt5.dll 导出符号**写的，例如：
//   ?text@QsciScintilla@@QEBA?AVQString@@H@Z          -> QString text(int) const
//   ?selectedText@QsciScintilla@@QEBA?AVQString@@XZ   -> QString selectedText() const
//   ?SendScintilla@QsciScintillaBase@@QEBAJIJ@Z       -> long SendScintilla(unsigned int, unsigned long, long) const
//   SCI_GETSELECTIONSTART = 2143 / SCI_GETSELECTIONEND = 2145
//   SCI_POINTXFROMPOSITION = 2164 / SCI_POINTYFROMPOSITION = 2165
//   SCI_LINEFROMPOSITION = 2166 / SCI_TEXTHEIGHT = 2279
// 所以它足以校验 nddhost.cpp / selectionassistant.cpp 的语法，
// 但不能替代真实头文件做 API 校验。
#pragma once
#include "_core.h"

class QsciScintillaBase : public QWidget
{
public:
	enum
	{
		SCI_GETSELECTIONSTART  = 2143,
		SCI_GETSELECTIONEND    = 2145,
		SCI_POINTXFROMPOSITION = 2164,
		SCI_POINTYFROMPOSITION = 2165,
		SCI_LINEFROMPOSITION   = 2166,
		SCI_TEXTHEIGHT         = 2279
	};

	long SendScintilla(unsigned int msg, unsigned long wParam = 0, long lParam = 0) const
	{
		(void)msg;
		(void)wParam;
		(void)lParam;
		return 0;
	}

	QWidget* viewport() const { return nullptr; }
};

class QsciScintilla : public QsciScintillaBase
{
public:
	virtual ~QsciScintilla() {}

	bool    isReadOnly() const { return false; }
	bool    hasSelectedText() const { return false; }
	QString selectedText() const { return QString(); }
	QString text(int length = -1) const
	{
		(void)length;
		return QString();
	}

	void replaceSelectedText(const QString&) {}
	void selectAll(bool select = true) { (void)select; }
	void beginUndoAction() {}
	void endUndoAction() {}
	void insert(const QString&) {}

signals:
	void selectionChanged();
};
