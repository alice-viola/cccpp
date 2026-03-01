#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QRegularExpression>

struct CodeBlockInfo {
    QString language;
    QString code;
    int startOffset = 0;
    int endOffset = 0;
};

class MarkdownRenderer : public QObject {
    Q_OBJECT
public:
    explicit MarkdownRenderer(QObject *parent = nullptr);

    QString toHtml(const QString &markdown) const;

    // Returns info about code blocks found during last toHtml() call
    QList<CodeBlockInfo> lastCodeBlocks() const { return m_lastCodeBlocks; }

    /// Regex for fenced code blocks — shared with ChatMessageWidget for Copy/Apply.
    static const QRegularExpression &fencedCodeRegex();

private:
    QString escapeHtml(const QString &text) const;
    QString extractFencedBlocks(const QString &text, QStringList &blocks) const;
    QString extractInlineCode(const QString &text, QStringList &blocks) const;
    QString processTables(const QString &text, QStringList &blocks) const;
    QString processInlineFormatting(const QString &text) const;

    static QString placeholder(int index);

    mutable QList<CodeBlockInfo> m_lastCodeBlocks;
};
