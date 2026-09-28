// 语法自检用的 JSON + 网络桩。被 _core.h 在末尾 include。
// 同样只保证"能解析"，不保证与真实 Qt 行为一致。
#pragma once

// ------------------------------------------------------------------ JSON

struct QJsonParseError
{
	enum ParseError
	{
		NoError,
		UnterminatedObject,
		MissingNameSeparator,
		UnterminatedArray,
		IllegalValue
	};

	ParseError error;

	QString errorString() const { return QString(); }
};

class QJsonObject;
class QJsonArray;

class QJsonValue
{
public:
	QJsonValue() {}
	QJsonValue(const QString&) {}
	QJsonValue(bool) {}
	QJsonValue(double) {}
	QJsonValue(int) {}
	QJsonValue(const QJsonObject&);
	QJsonValue(const QJsonArray&);

	QJsonObject toObject() const;
	QJsonArray  toArray() const;
	QString     toString() const { return QString(); }
	int         toInt() const { return 0; }
	bool        toBool() const { return false; }
	double      toDouble() const { return 0.0; }
};

class QJsonObject
{
public:
	void       insert(const QString&, const QJsonValue&) {}
	QJsonValue value(const QString&) const { return QJsonValue(); }
	bool       isEmpty() const { return true; }
};

class QJsonArray
{
public:
	void       append(const QJsonValue&) {}
	QJsonValue at(int) const { return QJsonValue(); }
	bool       isEmpty() const { return true; }
	int        size() const { return 0; }
};

inline QJsonValue::QJsonValue(const QJsonObject&) {}
inline QJsonValue::QJsonValue(const QJsonArray&) {}
inline QJsonObject QJsonValue::toObject() const { return QJsonObject(); }
inline QJsonArray  QJsonValue::toArray() const { return QJsonArray(); }

class QJsonDocument
{
public:
	enum JsonFormat
	{
		Indented,
		Compact
	};

	QJsonDocument() {}
	QJsonDocument(const QJsonObject&) {}

	bool        isObject() const { return true; }
	bool        isNull() const { return false; }
	QJsonObject object() const { return QJsonObject(); }

	QByteArray toJson(JsonFormat) const { return QByteArray(); }

	static QJsonDocument fromJson(const QByteArray&) { return QJsonDocument(); }
	static QJsonDocument fromJson(const QByteArray&, QJsonParseError*)
	{
		return QJsonDocument();
	}
};

// ------------------------------------------------------------------ 网络

class QUrl
{
public:
	QUrl() {}
	QUrl(const QString&) {}
};

class QNetworkRequest
{
public:
	enum Header
	{
		ContentTypeHeader,
		ContentLengthHeader
	};

	enum Attribute
	{
		HttpStatusCodeAttribute,
		RedirectionTargetAttribute
	};

	explicit QNetworkRequest(const QUrl&) {}

	void setHeader(Header, const QVariant&) {}
	void setRawHeader(const QByteArray&, const QByteArray&) {}
	void setTransferTimeout(int) {}
};

class QNetworkReply : public QObject
{
public:
	enum NetworkError
	{
		NoError,
		ConnectionRefusedError,
		RemoteHostClosedError,
		TimeoutError,
		OperationCanceledError
	};

	QByteArray   readAll() { return QByteArray(); }
	NetworkError error() const { return NoError; }
	QString      errorString() const { return QString(); }
	QVariant     attribute(QNetworkRequest::Attribute) const { return QVariant(); }
	void         abort() {}

signals:
	void finished();
};

class QNetworkAccessManager : public QObject
{
public:
	explicit QNetworkAccessManager(QObject* = nullptr) {}

	QNetworkReply* post(const QNetworkRequest&, const QByteArray&) { return nullptr; }
};
