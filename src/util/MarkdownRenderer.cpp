#include "util/MarkdownRenderer.h"
#include "ui/ThemeManager.h"
#include <QRegularExpression>
#include <QFileInfo>

MarkdownRenderer::MarkdownRenderer(QObject *parent)
    : QObject(parent)
{
}

QString MarkdownRenderer::toHtml(const QString &markdown) const
{
    m_lastCodeBlocks.clear();

    QString result = markdown;
    result = processCodeBlocks(result);
    result = processTables(result);
    result = processInlineFormatting(result);

    QStringList parts = result.split(QRegularExpression("(<pre[^>]*>[\\s\\S]*?</pre>|<table[^>]*>[\\s\\S]*?</table>)"));
    QString final;
    for (const QString &part : parts) {
        if (part.startsWith("<pre") || part.startsWith("<table") || part.startsWith("<div class='codeblock")) {
            final += part;
        } else {
            QString p = part;
            p.replace("\n\n", "</p><p style='margin:8px 0;'>");
            p.replace("\n", "<br>");
            // Block-level <div> elements (list items, headings) don't need <br> or
            // paragraph breaks between them — collapse any such spacing.
            p.replace(QRegularExpression("</div>((?:<br>|</p>\\s*<p[^>]*>|\\s)+)<div"), "</div><div");
            final += p;
        }
    }

    return QStringLiteral(
        "<div style='font-family:\"Inter\";"
        "font-size:13.5px;line-height:1.6;color:%2;'>"
        "<p style='margin:0;'>%1</p></div>")
        .arg(final, ThemeManager::instance().hex("text_primary"));
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

    QRegularExpression fenced("```(\\w*)\\n([\\s\\S]*?)\\n```");
    auto it = fenced.globalMatch(result);
    QList<std::pair<int, int>> ranges;
    QList<QString> replacements;

    int blockIndex = 0;
    while (it.hasNext()) {
        auto match = it.next();
        ranges.append({match.capturedStart(), match.capturedLength()});
        QString lang = match.captured(1);
        QString code = match.captured(2);
        QString escapedCode = escapeHtml(code);

        // Store code block info for Apply functionality
        CodeBlockInfo info;
        info.language = lang;
        info.code = code;
        info.startOffset = match.capturedStart();
        info.endOffset = match.capturedEnd();
        m_lastCodeBlocks.append(info);

        auto &tm = ThemeManager::instance();

        QString cardTitle;
        QString diffFilePath;
        bool isDiff = (lang == "diff");
        if (isDiff) {
            QRegularExpression diffFile("^\\+{3}\\s+(?:b/)?(.+?)(?:\\t.*)?$",
                                        QRegularExpression::MultilineOption);
            auto dm = diffFile.match(code);
            if (dm.hasMatch()) {
                diffFilePath = dm.captured(1).trimmed();
                QString fname = QFileInfo(diffFilePath).fileName();
                cardTitle = fname.isEmpty() ? diffFilePath : fname;
            }

            QStringList lines = escapedCode.split('\n');
            QStringList highlighted;
            for (const QString &line : lines) {
                if ((line.startsWith("---") || line.startsWith("+++")) && !cardTitle.isEmpty())
                    continue;

                QString color;
                if (line.startsWith('+'))
                    color = tm.hex("green");
                else if (line.startsWith('-'))
                    color = tm.hex("red");
                else if (line.startsWith("@@"))
                    color = tm.hex("blue");

                if (!color.isEmpty())
                    highlighted << "<span style='color:" + color + ";'>" + line + "</span>";
                else
                    highlighted << line;
            }
            escapedCode = highlighted.join('\n');
        }

        // Card header
        QString titleLabel;
        if (isDiff && !cardTitle.isEmpty()) {
            titleLabel = "<a href='cccpp://open?file=" + escapeHtml(diffFilePath)
                + "&amp;line=0' style='color:" + tm.hex("blue")
                + ";text-decoration:none;font-family:\"JetBrains Mono\";font-size:12px;'>"
                + escapeHtml(cardTitle) + "</a>";
        } else if (!lang.isEmpty()) {
            titleLabel = "<span style='color:" + tm.hex("text_muted")
                + ";font-family:\"JetBrains Mono\";font-size:11px;'>" + lang + "</span>";
        }

        QString copyApply =
            " <a href='cccpp://copy?block=" + QString::number(blockIndex)
            + "' style='color:" + tm.hex("text_muted")
            + ";text-decoration:none;font-size:11px;'>Copy</a>"
            " <a href='cccpp://apply?block=" + QString::number(blockIndex) + "&amp;lang=" + lang
            + "' style='color:" + tm.hex("on_accent")
            + ";text-decoration:none;font-size:11px;background:" + tm.hex("blue")
            + ";padding:1px 6px;'>Apply</a>";

        QString codeAsHtml = escapedCode;
        codeAsHtml.replace("\n", "<br>");

        QString border = tm.hex("border_standard");
        QString replacement =
            "<table cellspacing='0' cellpadding='0' style='width:100%;margin:6px 0;'>"
            "<tr><td style='background:" + tm.hex("bg_surface") + ";padding:6px 12px;"
            "border:1px solid " + border + ";'>"
            + titleLabel + copyApply +
            "</td></tr>"
            "<tr><td style='background:" + tm.hex("bg_base") + ";padding:8px 14px;"
            "border:1px solid " + border + ";border-top:none;"
            "font-family:\"JetBrains Mono\";font-size:12px;color:" + tm.hex("text_primary") + ";'>"
            + codeAsHtml +
            "</td></tr></table>";
        replacements.append(replacement);
        ++blockIndex;
    }

    for (int i = ranges.size() - 1; i >= 0; --i) {
        result.replace(ranges[i].first, ranges[i].second, replacements[i]);
    }

    // Inline code: `code`
    QRegularExpression inlineCode("`([^`]+)`");
    {
        auto &tm = ThemeManager::instance();
        result.replace(inlineCode,
            QStringLiteral(
            "<code style='background:%1;color:%2;padding:2px 6px;"
            "border-radius:4px;font-family:\"JetBrains Mono\";font-size:12px;'>\\1</code>")
            .arg(tm.hex("bg_raised"), tm.hex("text_secondary")));
    }

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
    QStringList parts = text.split(QRegularExpression("(<pre[^>]*>[\\s\\S]*?</pre>|<table[^>]*>[\\s\\S]*?</table>)"));
    QString result;

    for (const QString &part : parts) {
        if (part.startsWith("<pre") || part.startsWith("<table")) {
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

                auto &tm = ThemeManager::instance();
                QString borderColor = tm.hex("text_faint");
                QString table = QStringLiteral(
                    "<table cellspacing='0' cellpadding='6' "
                    "style='border-collapse:collapse;margin:6px 0;border:1px solid %1;'>")
                    .arg(borderColor);

                table += QStringLiteral("<tr>");
                for (int c = 0; c < headerCells.size(); ++c) {
                    QString a = c < aligns.size() ? aligns[c] : QStringLiteral("left");
                    table += QStringLiteral(
                        "<td style='border:1px solid %3;padding:4px 10px;"
                        "text-align:%1;font-weight:bold;background:%4;'>%2</td>")
                        .arg(a, headerCells[c].trimmed(), borderColor, tm.hex("bg_window"));
                }
                table += QStringLiteral("</tr>");

                i += 2;
                bool even = false;
                while (i < lines.size()) {
                    QString dataLine = lines[i].trimmed();
                    if (dataLine.isEmpty() || !dataLine.contains('|'))
                        break;

                    auto cells = splitTableRow(dataLine);
                    QString bg = even ? tm.hex("bg_surface") : QStringLiteral("transparent");
                    table += QStringLiteral("<tr>");
                    for (int c = 0; c < cells.size(); ++c) {
                        QString a = c < aligns.size() ? aligns[c] : QStringLiteral("left");
                        table += QStringLiteral(
                            "<td style='border:1px solid %3;padding:4px 10px;"
                            "text-align:%1;background:%2;'>%4</td>")
                            .arg(a, bg, borderColor, cells[c].trimmed());
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
    auto &tm = ThemeManager::instance();

    QRegularExpression bold("\\*\\*(.+?)\\*\\*");
    result.replace(bold, "<b>\\1</b>");

    QRegularExpression boldUnderscore("__(.+?)__");
    result.replace(boldUnderscore, "<b>\\1</b>");

    QRegularExpression italic("(?<!\\*)\\*([^*]+)\\*(?!\\*)");
    result.replace(italic, "<i>\\1</i>");

    QString headingColor = tm.hex("text_primary");
    QRegularExpression h1("^# (.+)$", QRegularExpression::MultilineOption);
    result.replace(h1,
        QStringLiteral("<div style='color:%1;margin:16px 0 6px;font-size:18px;font-weight:600;letter-spacing:-0.2px;'>\\1</div>")
        .arg(headingColor));

    QRegularExpression h2("^## (.+)$", QRegularExpression::MultilineOption);
    result.replace(h2,
        QStringLiteral("<div style='color:%1;margin:14px 0 4px;font-size:16px;font-weight:600;letter-spacing:-0.2px;'>\\1</div>")
        .arg(headingColor));

    QRegularExpression h3("^### (.+)$", QRegularExpression::MultilineOption);
    result.replace(h3,
        QStringLiteral("<div style='color:%1;margin:12px 0 4px;font-size:14px;font-weight:600;'>\\1</div>")
        .arg(headingColor));

    QString faintHex = tm.hex("text_faint");
    QRegularExpression bullet("^[\\-\\*] (.+)$", QRegularExpression::MultilineOption);
    result.replace(bullet,
        QStringLiteral("<div style='padding-left:20px;margin:2px 0;'>"
        "<span style='color:%1;'>&#x2022;</span> \\1</div>")
        .arg(faintHex));

    QRegularExpression numbered("^(\\d+)\\. (.+)$", QRegularExpression::MultilineOption);
    result.replace(numbered,
        QStringLiteral("<div style='padding-left:20px;margin:2px 0;'>"
        "<span style='color:%1;'>\\1.</span> \\2</div>")
        .arg(faintHex));

    QRegularExpression link("\\[([^\\]]+)\\]\\(([^)]+)\\)");
    result.replace(link,
        QStringLiteral("<a href='\\2' style='color:%1;text-decoration:underline;'>\\1</a>")
        .arg(tm.hex("blue")));

    QRegularExpression hr("^(---+|\\*\\*\\*+)$", QRegularExpression::MultilineOption);
    result.replace(hr,
        QStringLiteral("<hr style='border:none;border-top:1px solid %1;margin:8px 0;'>")
        .arg(tm.hex("border_standard")));

    QRegularExpression blockquote("^> (.+)$", QRegularExpression::MultilineOption);
    result.replace(blockquote,
        QStringLiteral("<div style='border-left:3px solid %1;padding-left:16px;margin:8px 0;"
        "color:%2;font-style:italic;'>\\1</div>")
        .arg(tm.hex("border_standard"), tm.hex("text_muted")));

    return result;
}
