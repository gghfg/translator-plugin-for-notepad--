// 语法自检用的最小 Qt 桩 —— 只为了让 MSVC 能真正解析本工程的 C++ 语法。
//
// ⚠️ 它不参与真实构建，也**不能**替代真实头文件做 API 校验：
//    桩是按"本工程用到的签名"写的，所以 API 用错它看不出来。
//    它能抓的是：语法错误、拼错的标识符、用了未声明/未定义的成员、类型不匹配。
//
// 覆盖：QString / QStringList / QVector / QSet / QChar / QLatin1Char / QByteArray /
//       QVariant / QFileInfo / QSettings / QObject / QMetaObject
//       （JSON 与网络部分见 _netjson.h）
//
// 刻意不 include 任何标准库头。translatorconfig.cpp 会 include <windows.h>，
// 编译它需要先跑 vcvars64.bat —— 见同目录 run.bat。
#pragma once

// 告诉代码"这是在 Windows 上编译"。
// 必需：真实 Qt 的 qglobal.h 在 Windows 上会定义 Q_OS_WIN，而桩不会。
// 少了它，`#ifdef Q_OS_WIN` 包起来的 DPAPI 加密代码会被整段跳过，
// 探测就会"看起来通过"其实根本没编译到那段——这正是本桩踩过的坑。
#ifndef Q_OS_WIN
#  define Q_OS_WIN 1
#endif

// ------------------------------------------------------------------ Qt 关键字宏

#define Q_OBJECT
#define signals public
#define slots
#define emit
#define Q_UNUSED(x) (void)(x)
#define QByteArrayLiteral(x) QByteArray((x), 0)
#define QStringLiteral(x) QString(x)

// ------------------------------------------------------------------ Qt 命名空间

namespace Qt
{
enum CaseSensitivity
{
	CaseInsensitive,
	CaseSensitive
};
}

// ------------------------------------------------------------------ 字符

class QLatin1Char;
class QByteArray; // 前置声明：QString 的 toUtf8()/toLatin1() 返回它

class QChar
{
public:
	QChar() : m_value(0) {}
	explicit QChar(char c) : m_value(c) {}

	bool isLetter() const { return true; }
	bool operator==(const QChar& other) const { return m_value == other.m_value; }

private:
	char m_value;
};

class QLatin1Char
{
public:
	explicit QLatin1Char(char c) : m_value(c) {}

	// 非 explicit，才能让 `ch == QLatin1Char('\n')` 成立
	operator QChar() const { return QChar(m_value); }

private:
	char m_value;
};

// ------------------------------------------------------------------ 字符串

class QString
{
public:
	QString() {}
	QString(const char*) {}

	int     size() const { return 0; }
	bool    isEmpty() const { return true; }
	QString mid(int, int = -1) const { return QString(); }
	QString trimmed() const { return QString(); }
	QString simplified() const { return QString(); }
	QChar   at(int) const { return QChar(); }
	void    clear() {}
	void    reserve(int) {}
	void    chop(int) {}

	bool startsWith(const QString&) const { return false; }
	bool endsWith(const QString&) const { return false; }
	bool endsWith(const QLatin1Char&) const { return false; }

	// 返回/接收不完整类型是合法的（仅声明）；定义放在 QByteArray 之后
	QByteArray toUtf8() const;
	QByteArray toLatin1() const;

	QString& operator+=(const QString&) { return *this; }

	QString arg(const QString&) const { return QString(); }
	QString arg(int) const { return QString(); }
	QString arg(double) const { return QString(); }
	QString arg(const QString&, const QString&) const { return QString(); }

	bool contains(const QString&, Qt::CaseSensitivity = Qt::CaseSensitive) const
	{
		return false;
	}

	int count(const QChar&) const { return 0; }

	static QString fromLatin1(const char*) { return QString(); }
	static QString fromUtf8(const char*) { return QString(); }
	static QString fromLatin1(const QByteArray&);
	static QString fromUtf8(const QByteArray&);
};

inline QString operator+(const QString&, const QString&) { return QString(); }
inline bool    operator==(const QString&, const QString&) { return true; }
inline bool    operator!=(const QString&, const QString&) { return false; }

class QStringList
{
public:
	void     append(const QString&) {}
	int      size() const { return 0; }
	bool     isEmpty() const { return true; }
	QString  at(int) const { return QString(); }
	QString& last() { return m_dummy; }
	QString& operator[](int) { return m_dummy; }
	QStringList& operator<<(const QString&) { return *this; }
	void     removeLast() {}
	void     clear() {}
	void     reserve(int) {}

private:
	QString m_dummy;
};

// ------------------------------------------------------------------ 容器

template <class T>
class QVector
{
public:
	QVector() {}
	explicit QVector(int) {}
	QVector(int, const T&) {}

	void append(const T&) {}
	int  size() const { return 0; }
	bool isEmpty() const { return true; }
	T    at(int) const { return T(); }
	void clear() {}
	void reserve(int) {}

	T&       operator[](int) { return m_dummy; }
	const T& operator[](int) const { return m_dummy; }

private:
	T m_dummy;
};

template <class T>
class QSet
{
public:
	QSet() : m_data(nullptr), m_size(0), m_cap(0) {}
	QSet(const QSet& other) : m_data(nullptr), m_size(0), m_cap(0) { assign(other); }
	~QSet() { delete[] m_data; }

	QSet& operator=(const QSet& other)
	{
		if (this != &other)
		{
			assign(other);
		}
		return *this;
	}

	void insert(const T& value)
	{
		if (m_size == m_cap)
		{
			grow();
		}
		m_data[m_size++] = value;
	}

	bool remove(const T& value)
	{
		for (int i = 0; i < m_size; ++i)
		{
			if (m_data[i] == value)
			{
				m_data[i] = m_data[--m_size];
				return true;
			}
		}
		return false;
	}

	void clear() { m_size = 0; }
	bool isEmpty() const { return m_size == 0; }
	int  size() const { return m_size; }

	// range-for 需要
	T*       begin() { return m_data; }
	T*       end() { return m_data + m_size; }
	const T* begin() const { return m_data; }
	const T* end() const { return m_data + m_size; }

private:
	void grow()
	{
		const int newCap = (m_cap == 0) ? 8 : m_cap * 2;
		T* newData = new T[newCap];
		for (int i = 0; i < m_size; ++i)
		{
			newData[i] = m_data[i];
		}
		delete[] m_data;
		m_data = newData;
		m_cap = newCap;
	}

	void assign(const QSet& other)
	{
		delete[] m_data;
		m_data = nullptr;
		m_size = 0;
		m_cap = 0;
		for (int i = 0; i < other.m_size; ++i)
		{
			insert(other.m_data[i]);
		}
	}

	T*  m_data;
	int m_size;
	int m_cap;
};

// ------------------------------------------------------------------ 字节数组

class QByteArray
{
public:
	QByteArray() {}
	QByteArray(const char*) {}
	QByteArray(const char*, int) {}

	int         size() const { return 0; }
	bool        isEmpty() const { return true; }
	const char* constData() const { return nullptr; }

	QByteArray toBase64() const { return QByteArray(); }
	QByteArray toUtf8() const { return QByteArray(); }
	QByteArray trimmed() const { return QByteArray(); }

	static QByteArray fromBase64(const QByteArray&) { return QByteArray(); }
	static QByteArray fromLatin1(const char*) { return QByteArray(); }
};

inline QByteArray operator+(const QByteArray&, const QByteArray&) { return QByteArray(); }

// QByteArray 现已完整，补上前面只做了声明的那些函数
inline QByteArray QString::toUtf8() const { return QByteArray(); }
inline QByteArray QString::toLatin1() const { return QByteArray(); }
inline QString    QString::fromLatin1(const QByteArray&) { return QString(); }
inline QString    QString::fromUtf8(const QByteArray&) { return QString(); }

// ------------------------------------------------------------------ QVariant

class QVariant
{
public:
	QVariant() {}
	QVariant(const QString&) {}
	QVariant(int) {}
	QVariant(double) {}
	QVariant(bool) {}
	QVariant(const QByteArray&) {}

	bool    isValid() const { return false; }
	QString toString() const { return QString(); }
	int     toInt() const { return 0; }
	double  toDouble() const { return 0.0; }
	bool    toBool() const { return false; }
};

// ------------------------------------------------------------------ QFileInfo / QSettings

class QFileInfo
{
public:
	static bool exists(const QString&) { return false; }
};

class QSettings
{
public:
	enum Format { IniFormat };
	enum Scope { UserScope };
	enum Status { NoError };

	QSettings(Format, Scope, const QString&, const QString& = QString()) {}

	void setIniCodec(const char*) {}

	QVariant value(const QString&) const { return QVariant(); }
	QVariant value(const QString&, const QVariant&) const { return QVariant(); }
	void     setValue(const QString&, const QVariant&) {}
	void     remove(const QString&) {}
	void     sync() {}

	Status  status() const { return NoError; }
	QString fileName() const { return QString(); }
};

// ------------------------------------------------------------------ QObject

class QMetaObject
{
public:
	const char* className() const { return ""; }
};

class QObject
{
public:
	QObject() {}
	explicit QObject(QObject*) {}
	virtual ~QObject() {}

	virtual const QMetaObject* metaObject() const { return nullptr; }

	QVariant property(const char*) const { return QVariant(); }
	bool     setProperty(const char*, const QVariant&) { return true; }

	void deleteLater() {}
	bool disconnect(const QObject*) { return true; }

	// 故意写成"照单全收"的可变参数模板：只为让 connect(...) / disconnect(...) 通过语法检查。
	// ⚠️ 因此它们**不会**校验信号槽签名是否匹配，这是本桩的已知盲区。
	//    但取信号成员指针（&QPushButton::clicked）本身仍会被检查，名字写错照样报错。
	template <typename... Args>
	static bool connect(Args&&...) { return true; }

	template <typename... Args>
	static bool disconnect(Args&&...) { return true; }
};

// JSON 与网络部分
#include "_netjson.h"

// Widgets 部分
#include "_widgets.h"
