#include "textsplit.h"

// 只 include <QChar>：QLatin1Char 就定义在 qchar.h 里，
// Qt5 并没有名为 <QLatin1Char> 的转发头文件，写它会直接 C1083。
#include <QChar>

namespace {

struct LineSlice
{
	QString content; // 不含换行符
	QString term;    // "\r\n" / "\n" / "\r" / ""（最后一行可能没有换行符）
};

// 按 CRLF / LF / CR 三种换行符切行，换行符原样保留，不做任何归一化。
QVector<LineSlice> parseLines(const QString& text)
{
	QVector<LineSlice> lines;

	const int n = text.size();
	int lineStart = 0;
	int i = 0;

	while (i < n)
	{
		const QChar ch = text.at(i);

		if (ch == QLatin1Char('\n'))
		{
			LineSlice slice;
			slice.content = text.mid(lineStart, i - lineStart);
			slice.term = QStringLiteral("\n");
			lines.append(slice);
			++i;
			lineStart = i;
		}
		else if (ch == QLatin1Char('\r'))
		{
			LineSlice slice;
			slice.content = text.mid(lineStart, i - lineStart);

			if (i + 1 < n && text.at(i + 1) == QLatin1Char('\n'))
			{
				slice.term = QStringLiteral("\r\n");
				i += 2;
			}
			else
			{
				slice.term = QStringLiteral("\r");
				++i;
			}

			lines.append(slice);
			lineStart = i;
		}
		else
		{
			++i;
		}
	}

	// 结尾没有换行符时补最后一行
	if (lineStart < n)
	{
		LineSlice slice;
		slice.content = text.mid(lineStart);
		slice.term = QString();
		lines.append(slice);
	}

	return lines;
}

bool isBlankLine(const QString& content)
{
	return content.trimmed().isEmpty();
}

} // namespace

bool needsTranslation(const QString& block)
{
	for (int i = 0; i < block.size(); ++i)
	{
		if (block.at(i).isLetter())
		{
			return true;
		}
	}

	return false;
}

DocumentParts splitDocument(const QString& text, int maxChunkChars)
{
	if (maxChunkChars < 1)
	{
		maxChunkChars = 1;
	}

	DocumentParts parts;
	parts.gaps.append(QString()); // gaps[0]

	const QVector<LineSlice> lines = parseLines(text);

	QString currentBlock;
	int     currentChars = 0;
	bool    inBlock = false;
	QString pendingTerm; // 上一行尚未归属的换行符

	for (int i = 0; i < lines.size(); ++i)
	{
		const LineSlice& line = lines.at(i);

		if (isBlankLine(line.content))
		{
			// 空行表示段落结束：先把当前块收尾，再把这行整体塞进后面的 gap
			if (inBlock)
			{
				parts.blocks.append(currentBlock);
				parts.gaps.append(pendingTerm);

				currentBlock.clear();
				currentChars = 0;
				inBlock = false;
				pendingTerm.clear();
			}

			parts.gaps.last() += line.content + line.term;
			pendingTerm.clear();
			continue;
		}

		// 段落过长时在行边界切一刀，避免单个请求超出 maxChunkChars
		if (inBlock && currentChars + line.content.size() > maxChunkChars)
		{
			parts.blocks.append(currentBlock);
			parts.gaps.append(pendingTerm);

			currentBlock.clear();
			currentChars = 0;
			pendingTerm.clear();
			// 本行成为新块的第一行，继续往下走
		}

		if (inBlock)
		{
			// 块内行与行之间的换行符留在块里，让模型看到原始换行
			currentBlock += pendingTerm;
		}

		currentBlock += line.content;
		currentChars += line.content.size();
		pendingTerm = line.term;
		inBlock = true;
	}

	if (inBlock)
	{
		parts.blocks.append(currentBlock);
		parts.gaps.append(pendingTerm);
		pendingTerm.clear();
	}

	// 正常情况下此时 pendingTerm 已空；留一行兜底，保证 gaps 一定比 blocks 多一个
	if (parts.gaps.size() != parts.blocks.size() + 1)
	{
		while (parts.gaps.size() < parts.blocks.size() + 1)
		{
			parts.gaps.append(QString());
		}
		while (parts.gaps.size() > parts.blocks.size() + 1)
		{
			parts.gaps.removeLast();
		}
	}

	return parts;
}

QString joinDocument(const DocumentParts& parts, const QStringList& translated)
{
	if (translated.size() != parts.blocks.size())
	{
		return QString();
	}

	QString out;
	out.reserve(translated.size() * 2);

	if (!parts.gaps.isEmpty())
	{
		out += parts.gaps.at(0);
	}

	for (int i = 0; i < translated.size(); ++i)
	{
		out += translated.at(i);

		const int gapIndex = i + 1;
		if (gapIndex < parts.gaps.size())
		{
			out += parts.gaps.at(gapIndex);
		}
	}

	return out;
}
