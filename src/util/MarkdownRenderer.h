#pragma once

#include <QObject>
#include <QString>
#include <QTextDocument>
#include <QList>

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

private:
    QString escapeHtml(const QString &text) const;
    QString processCodeBlocks(const QString &text) const;
    QString processTables(const QString &text) const;
    QString processInlineFormatting(const QString &text) const;

    mutable QList<CodeBlockInfo> m_lastCodeBlocks;
};
