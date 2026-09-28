// 语法自检用的 Qt Widgets 桩。被 _core.h 在末尾 include。
//
// 目的：让 translatepanel.cpp / settingsdialog.cpp / pluginentry.cpp 这三个
// 纯 UI 文件也能过真编译器，从而抓出语法错误、成员名拼错、信号名写错、
// 用了未声明的成员等问题。
//
// ⚠️ 已知盲区（必须清楚）：
//   1) QObject::connect 是"照单全收"的可变参数模板，因此**不校验信号与槽的参数是否匹配**。
//      但取信号成员指针（&QPushButton::clicked）本身仍会被检查——名字写错照样报错。
//   2) 每个方法签名都是按本工程用法写的，所以它证明不了"Qt API 用对了"，
//      只能证明"这段 C++ 能解析"。
#pragma once

// ------------------------------------------------------------------ Qt 枚举

namespace Qt
{
enum WindowType
{
	Widget              = 0x00000000,
	Window              = 0x00000001,
	Popup               = 0x00000008,
	FramelessWindowHint = 0x00000800
};
typedef int WindowFlags;

enum WidgetAttribute
{
	WA_StyledBackground = 1,
	WA_DeleteOnClose
};

enum FocusPolicy
{
	NoFocus    = 0,
	TabFocus   = 0x1,
	ClickFocus = 0x2,
	StrongFocus = TabFocus | ClickFocus
};

enum CursorShape
{
	ArrowCursor,
	PointingHandCursor
};

enum Key
{
	Key_Escape = 0x01000000
};

enum Orientation
{
	Horizontal,
	Vertical
};

enum TextInteractionFlag
{
	NoTextInteraction        = 0,
	TextSelectableByMouse    = 1,
	TextSelectableByKeyboard = 2
};
typedef int TextInteractionFlags;

enum DockWidgetArea
{
	LeftDockWidgetArea   = 0x1,
	RightDockWidgetArea  = 0x2,
	TopDockWidgetArea    = 0x4,
	BottomDockWidgetArea = 0x8
};
typedef int DockWidgetAreas;

inline DockWidgetAreas operator|(DockWidgetArea a, DockWidgetArea b)
{
	return int(a) | int(b);
}

inline WindowFlags operator|(WindowType a, WindowType b)
{
	return int(a) | int(b);
}
} // namespace Qt

// ------------------------------------------------------------------ 几何类型

class QPoint
{
public:
	QPoint() : m_x(0), m_y(0) {}
	QPoint(int x, int y) : m_x(x), m_y(y) {}

	int x() const { return m_x; }
	int y() const { return m_y; }

private:
	int m_x;
	int m_y;
};

class QSize
{
public:
	QSize() : m_w(0), m_h(0) {}
	QSize(int w, int h) : m_w(w), m_h(h) {}

	int width() const { return m_w; }
	int height() const { return m_h; }

private:
	int m_w;
	int m_h;
};

class QRect
{
public:
	QRect() : m_x(0), m_y(0), m_w(0), m_h(0) {}
	QRect(int x, int y, int w, int h) : m_x(x), m_y(y), m_w(w), m_h(h) {}

	int x() const { return m_x; }
	int y() const { return m_y; }
	int width() const { return m_w; }
	int height() const { return m_h; }

private:
	int m_x;
	int m_y;
	int m_w;
	int m_h;
};

template <typename T>
inline T qMax(T a, T b)
{
	return (a > b) ? a : b;
}

template <typename T>
inline T qMin(T a, T b)
{
	return (a < b) ? a : b;
}

template <typename T>
inline T qBound(T lo, T value, T hi)
{
	return (value < lo) ? lo : ((value > hi) ? hi : value);
}

// ------------------------------------------------------------------ 事件

class QKeyEvent
{
public:
	int key() const { return 0; }
	void accept() {}
};

class QSizePlaceholderRemoved;

// ------------------------------------------------------------------ QPointer

template <class T>
class QPointer
{
public:
	QPointer() : m_ptr(nullptr) {}
	QPointer(T* p) : m_ptr(p) {}

	T*   data() const { return m_ptr; }
	T*   operator->() const { return m_ptr; }
	operator T*() const { return m_ptr; }
	bool isNull() const { return m_ptr == nullptr; }

	QPointer& operator=(T* p)
	{
		m_ptr = p;
		return *this;
	}

private:
	T* m_ptr;
};

// qobject_cast 桩：只保证语法通过，不做类型校验
template <typename T>
T qobject_cast(QObject*)
{
	return nullptr;
}

// ------------------------------------------------------------------ 基础控件

class QWidget : public QObject
{
public:
	explicit QWidget(QWidget* parent = nullptr) : m_parent(parent) {}
	virtual ~QWidget() {}

	void setEnabled(bool) {}
	void setVisible(bool) {}
	void setToolTip(const QString&) {}
	void setFixedWidth(int) {}
	void setFixedHeight(int) {}
	void setFixedSize(int, int) {}
	void setWindowTitle(const QString&) {}
	void setStyleSheet(const QString&) {}
	void setObjectName(const QString&) {}
	void setMinimumWidth(int) {}
	void setMinimumHeight(int) {}
	void setWindowFlag(Qt::WindowType, bool = true) {}
	void setWindowFlags(Qt::WindowFlags) {}
	void setAttribute(Qt::WidgetAttribute, bool = true) {}
	void setFocusPolicy(Qt::FocusPolicy) {}
	void setCursor(Qt::CursorShape) {}

	void resize(int, int) {}
	void move(int, int) {}
	void move(const QPoint&) {}
	void show() {}
	void hide() {}
	void raise() {}
	bool isVisible() const { return false; }

	void     setParent(QWidget* parent) { m_parent = parent; }
	QWidget* parentWidget() const { return m_parent; }

	QSize  size() const { return QSize(); }
	int    width() const { return 0; }
	int    height() const { return 0; }
	QPoint mapToGlobal(const QPoint&) const { return QPoint(); }
	QPoint mapTo(QWidget*, const QPoint&) const { return QPoint(); }
	void   activateWindow() {}

protected:
	// 真实 QWidget 里是 virtual，浮窗要重写它处理 Esc
	virtual void keyPressEvent(QKeyEvent*) {}

	QWidget* m_parent;
};

class QLabel : public QWidget
{
public:
	QLabel(QWidget* = nullptr) {}
	QLabel(const QString&, QWidget* = nullptr) {}

	void setText(const QString&) {}
	void clear() {}
	void setWordWrap(bool) {}
	void setTextInteractionFlags(Qt::TextInteractionFlags) {}
};

class QAbstractButton : public QWidget
{
public:
	explicit QAbstractButton(QWidget* parent = nullptr) : QWidget(parent) {}

	void    setText(const QString&) {}
	QString text() const { return QString(); }
	void    setCheckable(bool) {}
	void    setChecked(bool) {}
	bool    isChecked() const { return false; }

signals:
	void clicked(bool checked = false);
	void toggled(bool checked);
};

class QPushButton : public QAbstractButton
{
public:
	explicit QPushButton(QWidget* = nullptr) {}
	QPushButton(const QString&, QWidget* = nullptr) {}

	void setDefault(bool) {}
};

class QCheckBox : public QAbstractButton
{
public:
	explicit QCheckBox(QWidget* = nullptr) {}
	QCheckBox(const QString&, QWidget* = nullptr) {}
};

class QLineEdit : public QWidget
{
public:
	enum EchoMode
	{
		Normal,
		NoEcho,
		Password,
		PasswordEchoOnEdit
	};

	explicit QLineEdit(QWidget* = nullptr) {}

	void    setEchoMode(EchoMode) {}
	void    setPlaceholderText(const QString&) {}
	void    setText(const QString&) {}
	QString text() const { return QString(); }
};

class QComboBox : public QWidget
{
public:
	explicit QComboBox(QWidget* = nullptr) {}

	void    setEditable(bool) {}
	void    addItem(const QString&) {}
	void    addItems(const QStringList&) {}
	int     findText(const QString&) const { return -1; }
	int     currentIndex() const { return -1; }
	void    setCurrentIndex(int) {}
	QString currentText() const { return QString(); }

signals:
	void currentIndexChanged(int index);
	void currentTextChanged(const QString& text);
};

class QPlainTextEdit : public QWidget
{
public:
	explicit QPlainTextEdit(QWidget* = nullptr) {}

	void    setPlaceholderText(const QString&) {}
	void    setPlainText(const QString&) {}
	QString toPlainText() const { return QString(); }
	void    setReadOnly(bool) {}
	void    setTabChangesFocus(bool) {}

signals:
	void textChanged();
};

class QAbstractSpinBox : public QWidget
{
public:
	explicit QAbstractSpinBox(QWidget* = nullptr) {}

	void setRange(int, int) {}
	void setSingleStep(int) {}
	void setSuffix(const QString&) {}
	void setValue(int) {}
	int  value() const { return 0; }

signals:
	void valueChanged(int);
};

class QSpinBox : public QAbstractSpinBox
{
public:
	explicit QSpinBox(QWidget* = nullptr) {}
};

class QDoubleSpinBox : public QWidget
{
public:
	explicit QDoubleSpinBox(QWidget* = nullptr) {}

	void   setRange(double, double) {}
	void   setSingleStep(double) {}
	void   setDecimals(int) {}
	void   setValue(double) {}
	double value() const { return 0.0; }

signals:
	void valueChanged(double);
};

class QProgressBar : public QWidget
{
public:
	explicit QProgressBar(QWidget* = nullptr) {}

	void setRange(int, int) {}
	void setValue(int) {}
	void setTextVisible(bool) {}
};

class QGroupBox : public QWidget
{
public:
	explicit QGroupBox(QWidget* = nullptr) {}
	QGroupBox(const QString&, QWidget* = nullptr) {}
};

// ------------------------------------------------------------------ 布局

class QLayout : public QObject
{
public:
	explicit QLayout(QWidget* = nullptr) {}

	void addWidget(QWidget*) {}
	void addWidget(QWidget*, int) {}
	void addLayout(QLayout*) {}
	void addStretch(int) {}
	void setContentsMargins(int, int, int, int) {}
};

class QBoxLayout : public QLayout
{
public:
	explicit QBoxLayout(QWidget* = nullptr) {}
};

class QHBoxLayout : public QBoxLayout
{
public:
	QHBoxLayout() {}
	// 注意：真实 Qt 这里是 explicit QHBoxLayout(QWidget *parent)，没有默认参数。
	// 若给默认参数，`new QHBoxLayout()` 会与无参构造产生二义性（本桩踩过）。
	explicit QHBoxLayout(QWidget* parent) : QBoxLayout(parent) {}
};

class QVBoxLayout : public QBoxLayout
{
public:
	QVBoxLayout() {}
	explicit QVBoxLayout(QWidget* parent) : QBoxLayout(parent) {}
};

class QFormLayout : public QLayout
{
public:
	enum FieldGrowthPolicy
	{
		FieldsStayAtSizeHint,
		ExpandingFieldsGrow,
		AllNonFixedFieldsGrow
	};

	explicit QFormLayout(QWidget* = nullptr) {}

	void setFieldGrowthPolicy(FieldGrowthPolicy) {}
	void addRow(QWidget*) {}
	void addRow(const QString&, QWidget*) {}
	void addRow(const QString&, QLayout*) {}
};

class QSplitter : public QWidget
{
public:
	QSplitter(Qt::Orientation, QWidget* = nullptr) {}

	void addWidget(QWidget*) {}
	void setStretchFactor(int, int) {}
};

// ------------------------------------------------------------------ 对话框 / 主窗口

class QDialog : public QWidget
{
public:
	enum DialogCode
	{
		Rejected,
		Accepted
	};

	explicit QDialog(QWidget* = nullptr) {}

	int exec() { return 0; }
	void accept() {}
	void reject() {}
};

class QDockWidgetPlaceholder;

class QMainWindow : public QWidget
{
public:
	explicit QMainWindow(QWidget* = nullptr) {}

	void addDockWidget(Qt::DockWidgetArea, QDockWidgetPlaceholder*);
};

class QDockWidgetPlaceholder : public QWidget
{
public:
	explicit QDockWidgetPlaceholder(QWidget* = nullptr) {}
};

class QDialogButtonBox : public QWidget
{
public:
	enum ButtonRole
	{
		InvalidRole,
		AcceptRole,
		RejectRole,
		DestructiveRole,
		ActionRole,
		HelpRole,
		YesRole,
		NoRole,
		ResetRole,
		ApplyRole
	};

	explicit QDialogButtonBox(QWidget* = nullptr) {}

	void addButton(QWidget*, ButtonRole) {}

signals:
	void accepted();
	void rejected();
};

// ------------------------------------------------------------------ 菜单 / 动作

class QKeySequence
{
public:
	QKeySequence() {}
	QKeySequence(const QString&) {}
};

class QAction : public QObject
{
public:
	QAction(QObject* = nullptr) {}
	QAction(const QString&, QObject* = nullptr) {}

	void setCheckable(bool) {}
	void setChecked(bool) {}
	bool isChecked() const { return false; }
	void setShortcut(const QKeySequence&) {}
	void setText(const QString&) {}

signals:
	void triggered(bool checked = false);
	void toggled(bool checked);
};

class QMenu : public QWidget
{
public:
	QMenu(QWidget* = nullptr) {}
	QMenu(const QString&, QWidget* = nullptr) {}

	QAction* addAction(const QString&) { return nullptr; }
	QAction* addAction(QAction*) { return nullptr; }
	void     addSeparator() {}
	QMenu*   addMenu(const QString&) { return nullptr; }
	void     addMenu(QMenu*) {}
	void     clear() {}
};

class QDockWidget : public QWidget
{
public:
	enum DockWidgetFeature
	{
		DockWidgetClosable         = 0x01,
		DockWidgetMovable          = 0x02,
		DockWidgetFloatable        = 0x04,
		DockWidgetVerticalTitleBar = 0x08
	};
	typedef int DockWidgetFeatures;

	QDockWidget(const QString&, QWidget* = nullptr) {}

	void setAllowedAreas(Qt::DockWidgetAreas) {}
	void setFeatures(DockWidgetFeatures) {}
	void setWidget(QWidget*) {}

signals:
	void visibilityChanged(bool visible);
};

inline QDockWidget::DockWidgetFeatures operator|(QDockWidget::DockWidgetFeature a,
												 QDockWidget::DockWidgetFeature b)
{
	return int(a) | int(b);
}

inline QDockWidget::DockWidgetFeatures operator|(QDockWidget::DockWidgetFeatures a,
												 QDockWidget::DockWidgetFeature b)
{
	return a | int(b);
}

// ------------------------------------------------------------------ 其它

class QTimer : public QObject
{
public:
	explicit QTimer(QObject* = nullptr) {}

	void setInterval(int) {}
	void setSingleShot(bool) {}
	void start() {}

signals:
	void timeout();
};

class QClipboard : public QObject
{
public:
	void setText(const QString&) {}
};

class QApplication
{
public:
	static QClipboard* clipboard() { return nullptr; }
};

class QMessageBox
{
public:
	static void information(QWidget*, const QString&, const QString&) {}
};

class QToolButton : public QWidget
{
public:
	explicit QToolButton(QWidget* parent = nullptr) : QWidget(parent) {}

	void setText(const QString&) {}

signals:
	void clicked(bool checked = false);
};

class QScreen
{
public:
	QRect availableGeometry() const { return QRect(); }
};

class QGuiApplication
{
public:
	static QScreen* screenAt(const QPoint&) { return nullptr; }
	static QScreen* primaryScreen() { return nullptr; }
};
