#include "util/MarkdownRenderer.h"
#include "ui/ThemeManager.h"
#include <QRegularExpression>
#include <QFileInfo>

// ---------------------------------------------------------------------------
// Placeholder helpers — control characters that never appear in markdown.
// Format:  \x01\x02{index}\x02\x01
// These are inert to every inline-formatting regex (no |, *, #, -, etc.).
// ---------------------------------------------------------------------------

QString MarkdownRenderer::placeholder(int index)
{
    return QStringLiteral("\x01\x02%1\x02\x01").arg(index);
}

MarkdownRenderer::MarkdownRenderer(QObject *parent)
    : QObject(parent)
{
}

const QRegularExpression &MarkdownRenderer::fencedCodeRegex()
{
    // [^\n`]*  — any language tag incl. hyphens/plus (objective-c, c++, etc.)
    // [ \t]*   — allows indented closing backticks (horizontal ws only, not \s)
    static QRegularExpression re("```([^\\n`]*)\\n([\\s\\S]*?)\\n[ \\t]*```");
    return re;
}

// ---------------------------------------------------------------------------
// Main entry point
//
// Pipeline (placeholder-based protection):
//   1. Extract fenced code blocks  → placeholders
//   2. Extract inline code (`...`) → placeholders
//   3. Convert markdown tables     → HTML → placeholders
//   4. Inline formatting (only raw markdown text remains — safe)
//   5. Newline → <br> conversion
//   6. Reinsert all placeholders
// ---------------------------------------------------------------------------

QString MarkdownRenderer::toHtml(const QString &markdown) const
{
    m_lastCodeBlocks.clear();

    QStringList blocks;        // rendered HTML keyed by placeholder index
    QString result = markdown;

    // Phase 1 — fenced code blocks first so inline-code regex can't reach
    //           inside them.
    result = extractFencedBlocks(result, blocks);

    // Phase 2 — inline code next so | inside `code` won't break table
    //           cell splitting.
    result = extractInlineCode(result, blocks);

    // Phase 3 — markdown tables → <table> HTML → placeholders.
    result = processTables(result, blocks);

    // Phase 4 — inline formatting runs on pure markdown text (safe).
    result = processInlineFormatting(result);

    // Phase 5 — newline conversion (placeholders are opaque tokens).
    result.replace("\n\n", "<br><br>");
    result.replace("\n", "<br>");

    // Collapse <br> between adjacent block-level <div> elements.
    static QRegularExpression divGap("</div>((?:<br>|\\s)+)<div");
    result.replace(divGap, "</div><div");

    // Phase 6 — reinsert all protected blocks.
    // Blocks are ordered: fenced code (low) → inline code → tables (high).
    // Table cell content may reference inline-code placeholders, so we must
    // insert tables first (high → low) so that nested placeholders are already
    // in `result` by the time their own index is processed.
    for (int i = blocks.size() - 1; i >= 0; --i)
        result.replace(placeholder(i), blocks[i]);

    return QStringLiteral(
        "<div style='font-family:\"Inter\";"
        "font-size:13.5px;line-height:1.6;color:%2;'>"
        "%1</div>")
        .arg(result, ThemeManager::instance().hex("text_primary"));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString MarkdownRenderer::escapeHtml(const QString &text) const
{
    QString escaped = text;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    escaped.replace("\"", "&quot;");
    return escaped;
}

// ---------------------------------------------------------------------------
// Phase 1 — Fenced code blocks
// ---------------------------------------------------------------------------

QString MarkdownRenderer::extractFencedBlocks(const QString &text, QStringList &blocks) const
{
    QString result = text;
    const QRegularExpression &fenced = fencedCodeRegex();
    auto it = fenced.globalMatch(result);

    QList<std::pair<int, int>> ranges;
    QList<QString> replacements;

    int blockIndex = 0;
    while (it.hasNext()) {
        auto match = it.next();
        ranges.append({match.capturedStart(), match.capturedLength()});

        QString lang = match.captured(1).trimmed();
        QString code = match.captured(2);
        QString escapedCode = escapeHtml(code);

        // Store code block info for Copy / Apply
        CodeBlockInfo info;
        info.language = lang;
        info.code = code;
        info.startOffset = match.capturedStart();
        info.endOffset = match.capturedEnd();
        m_lastCodeBlocks.append(info);

        auto &tm = ThemeManager::instance();

        // --- Diff highlighting ---
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

        // --- Card header ---
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
        QString html =
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

        int phIdx = blocks.size();
        blocks.append(html);
        replacements.append(placeholder(phIdx));
        ++blockIndex;
    }

    for (int i = ranges.size() - 1; i >= 0; --i)
        result.replace(ranges[i].first, ranges[i].second, replacements[i]);

    return result;
}

// ---------------------------------------------------------------------------
// Phase 2 — Inline code
// ---------------------------------------------------------------------------

QString MarkdownRenderer::extractInlineCode(const QString &text, QStringList &blocks) const
{
    QString result = text;

    // [^`\n]+ — don't match across newlines; prevents swallowing failed
    //           fenced blocks that the fenced regex didn't recognise.
    static QRegularExpression inlineCode("`([^`\\n]+)`");

    auto it = inlineCode.globalMatch(result);
    QList<std::pair<int, int>> ranges;
    QList<QString> replacements;

    auto &tm = ThemeManager::instance();
    while (it.hasNext()) {
        auto match = it.next();
        ranges.append({match.capturedStart(), match.capturedLength()});

        QString html = QStringLiteral(
            "<code style='background:%1;color:%2;padding:2px 6px;"
            "border-radius:4px;font-family:\"JetBrains Mono\";font-size:12px;'>%3</code>")
            .arg(tm.hex("bg_raised"), tm.hex("text_secondary"),
                 escapeHtml(match.captured(1)));

        int phIdx = blocks.size();
        blocks.append(html);
        replacements.append(placeholder(phIdx));
    }

    for (int i = ranges.size() - 1; i >= 0; --i)
        result.replace(ranges[i].first, ranges[i].second, replacements[i]);

    return result;
}

// ---------------------------------------------------------------------------
// Phase 3 — Tables
// ---------------------------------------------------------------------------

static QStringList splitTableRow(const QString &row)
{
    QString trimmed = row.trimmed();
    if (trimmed.startsWith('|')) trimmed = trimmed.mid(1);
    if (trimmed.endsWith('|'))   trimmed.chop(1);
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

QString MarkdownRenderer::processTables(const QString &text, QStringList &blocks) const
{
    // At this point the text contains only raw markdown + placeholders.
    // Placeholders don't contain '|', so splitTableRow is safe.

    QStringList lines = text.split('\n');
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

            int phIdx = blocks.size();
            blocks.append(table);
            output << placeholder(phIdx);
            continue;
        }

        output << lines[i];
        ++i;
    }

    return output.join('\n');
}

// ---------------------------------------------------------------------------
// Phase 4 — Inline formatting
//
// Only raw markdown text + inert placeholders reach this point.
// No HTML to corrupt.
// ---------------------------------------------------------------------------

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
