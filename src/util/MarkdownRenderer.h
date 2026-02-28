#pragma once

#include <QObject>
#include <QString>
#include <QTextDocument>

class MarkdownRenderer : public QObject {
    Q_OBJECT
public:
    explicit MarkdownRenderer(QObject *parent = nullptr);

    // Convert markdown text to HTML suitable for QTextBrowser display.
    // Phase 7 will replace this with MD4C + custom QPainter rendering.
    QString toHtml(const QString &markdown) const;

private:
    QString escapeHtml(const QString &text) const;
    QString processCodeBlocks(const QString &text) const;
    QString processInlineFormatting(const QString &text) const;
};
