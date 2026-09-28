#pragma once

// 整篇文档切分：把文档拆成“段落块 + 原样保留的分隔符”，翻译后可以精确拼回。
//
// 为什么要这么麻烦：直接把全文丢给模型，模型必然重排段落、统一换行符，
// 译完之后排版就废了。所以只把“真正需要翻译的文本”送出去，
// 所有空白、空行、换行符（含 CRLF/LF/CR 混用）都由本地原样保留。
//
// 数据模型：
//   blocks.size() == gaps.size() - 1
//   还原结果 = gaps[0] + 译文[0] + gaps[1] + 译文[1] + ... + 译文[n-1] + gaps[n]
// 因此只要 blocks 数量和顺序对上，任何空白都不会丢。

#include <QString>
#include <QStringList>
#include <QVector>

struct DocumentParts
{
	QStringList blocks; // 待翻译的段落（块内行间换行符原样保留，块尾换行符归入 gaps）
	QStringList gaps;   // 大小恒为 blocks.size() + 1

	bool isEmpty() const { return blocks.isEmpty(); }
};

// 把文档切成段落块。超过 maxChunkChars 的段落会在行边界处继续切分，
// 以保证单个请求不会过长。
DocumentParts splitDocument(const QString& text, int maxChunkChars);

// 用译文拼回整篇文档。translated 必须与 parts.blocks 等长；
// 长度不一致时返回空串（调用方应视为错误，避免把文档写坏）。
QString joinDocument(const DocumentParts& parts, const QStringList& translated);

// 该段落是否值得送去翻译。
// 纯空白、纯数字、纯符号的段落直接跳过，既省 token 也避免模型乱改。
bool needsTranslation(const QString& block);
