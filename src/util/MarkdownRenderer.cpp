#include "util/MarkdownRenderer.h"
#include <QRegularExpression>

MarkdownRenderer::MarkdownRenderer(QObject *parent)
    : QObject(parent)
{
}

QString MarkdownRenderer::toHtml(const QString &markdown) const
{
    QString result = markdown;

    // Process code blocks first (before other formatting touches backticks)
    result = processCodeBlocks(result);

    // Process markdown tables (before inline formatting so bold/italic still works in cells)
    result = processTables(result);

    // Process headers, bold, italic, etc.
    result = processInlineFormatting(result);

    // Convert double newlines to paragraph breaks, single to <br>
    // But not inside <pre> blocks
    QStringList parts = result.split(QRegularExpression("(<pre[^>]*>[\\s\\S]*?</pre>|<table[^>]*>[\\s\\S]*?</table>)"));
    QString final;
    for (const QString &part : parts) {
        if (part.startsWith("<pre") || part.startsWith("<table")) {
            final += part;
        } else {
            QString p = part;
            p.replace("\n\n", "</p><p style='margin:4px 0;'>");
            p.replace("\n", "<br>");
            final += p;
        }
    }

    return QStringLiteral(
        "<div style='font-family:\"Helvetica Neue\",sans-serif;"
        "font-size:13px;line-height:1.3;color:#cdd6f4;'>"
        "<p style='margin:0;'>%1</p></div>").arg(final);
}

QString MarkdownRenderer::escapeHtml(const QString &text) const
{
    QString escaped = text;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    escaped.replace("\"", "&quot;");
    return escaped;
}

QString MarkdownRenderer::processCodeBlocks(const QString &text) const
{
    QString result = text;

    // Fenced code blocks: ```lang\n...\n```
    QRegularExpression fenced("```(\\w*)\\n([\\s\\S]*?)\\n```");
    auto it = fenced.globalMatch(result);
    QList<std::pair<int, int>> ranges;
    QList<QString> replacements;

    while (it.hasNext()) {
        auto match = it.next();
        ranges.append({match.capturedStart(), match.capturedLength()});
        QString lang = match.captured(1);
        QString code = escapeHtml(match.captured(2));

        QString langTag;
        if (!lang.isEmpty())
            langTag = QStringLiteral(
                "<div style='background:#0e0e0e;color:#6c7086;font-size:11px;"
                "padding:4px 8px;border-radius:4px 4px 0 0;font-family:monospace;'>%1</div>").arg(lang);

        QString replacement = QStringLiteral(
            "%1<pre style='background:#0e0e0e;color:#cdd6f4;padding:6px 8px;"
            "border-radius:%2;font-family:Menlo,monospace;"
            "font-size:12px;overflow-x:auto;margin:2px 0 4px;line-height:1.3;"
            "border:1px solid #2a2a2a;'><code>%3</code></pre>")
            .arg(langTag,
                 lang.isEmpty() ? "4px" : "0 0 4px 4px",
                 code);
        replacements.append(replacement);
    }

    // Replace from end to start to preserve offsets
    for (int i = ranges.size() - 1; i >= 0; --i) {
        result.replace(ranges[i].first, ranges[i].second, replacements[i]);
    }

    // Inline code: `code`
    QRegularExpression inlineCode("`([^`]+)`");
    result.replace(inlineCode,
        "<code style='background:#252525;color:#cba6f7;padding:2px 5px;"
        "border-radius:4px;font-family:\"Menlo\",monospace;font-size:12px;'>\\1</code>");

    return result;
}

static QStringList splitTableRow(const QString &row)
{
    QString trimmed = row.trimmed();
    if (trimmed.startsWith('|')) trimmed = trimmed.mid(1);
    if (trimmed.endsWith('|')) trimmed.chop(1);
    return trimmed.split('|');
}

static bool isTableSeparator(const QString &line)
{
    QString trimmed = line.trimmed();
    if (!trimmed.contains('-') || !trimmed.contains('|'))
        return false;

    QStringList cells = splitTableRow(trimmed);
    if (cells.isEmpty())
        return false;

    static QRegularExpression cellPat("^\\s*:?-{3,}:?\\s*$");
    for (const QString &cell : cells) {
        if (!cellPat.match(cell).hasMatch())
            return false;
    }
    return true;
}

static QStringList parseAlignments(const QString &separator)
{
    QStringList cells = splitTableRow(separator);
    QStringList aligns;
    for (const QString &cell : cells) {
        QString c = cell.trimmed();
        bool l = c.startsWith(':');
        bool r = c.endsWith(':');
        if (l && r)        aligns << "center";
        else if (r)         aligns << "right";
        else                aligns << "left";
    }
    return aligns;
}

QString MarkdownRenderer::processTables(const QString &text) const
{
    QStringList parts = text.split(QRegularExpression("(<pre[^>]*>[\\s\\S]*?</pre>)"));
    QString result;

    for (const QString &part : parts) {
        if (part.startsWith("<pre")) {
            result += part;
            continue;
        }

        QStringList lines = part.split('\n');
        QStringList output;
        int i = 0;

        while (i < lines.size()) {
            QString line = lines[i].trimmed();

            if (i + 1 < lines.size()
                && line.contains('|') && line.count('|') >= 2
                && isTableSeparator(lines[i + 1])) {

                auto headerCells = splitTableRow(line);
                auto aligns = parseAlignments(lines[i + 1].trimmed());

                QString table = QStringLiteral(
                    "<table cellspacing='0' cellpadding='6' "
                    "style='border-collapse:collapse;margin:6px 0;border:1px solid #45475a;'>");

                table += QStringLiteral("<tr>");
                for (int c = 0; c < headerCells.size(); ++c) {
                    QString a = c < aligns.size() ? aligns[c] : QStringLiteral("left");
                    table += QStringLiteral(
                        "<td style='border:1px solid #45475a;padding:4px 10px;"
                        "text-align:%1;font-weight:bold;background:#1a1a1a;'>%2</td>")
                        .arg(a, headerCells[c].trimmed());
                }
                table += QStringLiteral("</tr>");

                i += 2;
                bool even = false;
                while (i < lines.size()) {
                    QString dataLine = lines[i].trimmed();
                    if (dataLine.isEmpty() || !dataLine.contains('|'))
                        break;

                    auto cells = splitTableRow(dataLine);
                    QString bg = even ? QStringLiteral("#141414") : QStringLiteral("transparent");
                    table += QStringLiteral("<tr>");
                    for (int c = 0; c < cells.size(); ++c) {
                        QString a = c < aligns.size() ? aligns[c] : QStringLiteral("left");
                        table += QStringLiteral(
                            "<td style='border:1px solid #45475a;padding:4px 10px;"
                            "text-align:%1;background:%2;'>%3</td>")
                            .arg(a, bg, cells[c].trimmed());
                    }
                    table += QStringLiteral("</tr>");
                    even = !even;
                    ++i;
                }

                table += QStringLiteral("</table>");
                output << table;
                continue;
            }

            output << lines[i];
            ++i;
        }

        result += output.join('\n');
    }

    return result;
}

QString MarkdownRenderer::processInlineFormatting(const QString &text) const
{
    QString result = text;

    // Bold: **text** or __text__
    QRegularExpression bold("\\*\\*(.+?)\\*\\*");
    result.replace(bold, "<b>\\1</b>");

    QRegularExpression boldUnderscore("__(.+?)__");
    result.replace(boldUnderscore, "<b>\\1</b>");

    // Italic: *text* or _text_
    QRegularExpression italic("(?<!\\*)\\*([^*]+)\\*(?!\\*)");
    result.replace(italic, "<i>\\1</i>");

    // Headers — compact margins
    QRegularExpression h1("^# (.+)$", QRegularExpression::MultilineOption);
    result.replace(h1,
        "<div style='color:#89b4fa;margin:6px 0 2px;font-size:15px;font-weight:600;'>\\1</div>");

    QRegularExpression h2("^## (.+)$", QRegularExpression::MultilineOption);
    result.replace(h2,
        "<div style='color:#89b4fa;margin:5px 0 2px;font-size:14px;font-weight:600;'>\\1</div>");

    QRegularExpression h3("^### (.+)$", QRegularExpression::MultilineOption);
    result.replace(h3,
        "<div style='color:#89b4fa;margin:4px 0 1px;font-size:13px;font-weight:600;'>\\1</div>");

    // Bullet lists
    QRegularExpression bullet("^[\\-\\*] (.+)$", QRegularExpression::MultilineOption);
    result.replace(bullet,
        "<div style='padding-left:12px;margin:1px 0;'>"
        "<span style='color:#6c7086;'>&#x2022;</span> \\1</div>");

    // Numbered lists: 1. item
    QRegularExpression numbered("^(\\d+)\\. (.+)$", QRegularExpression::MultilineOption);
    result.replace(numbered,
        "<div style='padding-left:16px;margin:2px 0;'>"
        "<span style='color:#6c7086;'>\\1.</span> \\2</div>");

    // Links: [text](url)
    QRegularExpression link("\\[([^\\]]+)\\]\\(([^)]+)\\)");
    result.replace(link,
        "<a href='\\2' style='color:#89b4fa;text-decoration:underline;'>\\1</a>");

    // Horizontal rule: --- or ***
    QRegularExpression hr("^(---+|\\*\\*\\*+)$", QRegularExpression::MultilineOption);
    result.replace(hr,
        "<hr style='border:none;border-top:1px solid #2a2a2a;margin:8px 0;'>");

    // Blockquote: > text
    QRegularExpression blockquote("^> (.+)$", QRegularExpression::MultilineOption);
    result.replace(blockquote,
        "<div style='border-left:3px solid #89b4fa;padding-left:12px;margin:4px 0;"
        "color:#a6adc8;font-style:italic;'>\\1</div>");

    return result;
}
