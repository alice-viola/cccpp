#include "ui/ToolCallGroupWidget.h"
#include "ui/ThemeManager.h"
#include <QScrollArea>
#include <QTimer>

static QString toolIcon(const QString &toolName)
{
    // Use simple ASCII/BMP symbols that work reliably with QStringLiteral
    if (toolName == "Edit" || toolName == "StrReplace")
        return QStringLiteral("\u270f");       // pencil
    if (toolName == "Write" || toolName == "NotebookEdit")
        return QStringLiteral("\u2710");       // upper right pencil
    if (toolName == "Read")
        return QStringLiteral("\u25A3");       // white square with small square (book-like)
    if (toolName == "Grep" || toolName == "Glob" || toolName == "Search")
        return QStringLiteral("\u2315");       // telephone recorder (search-like)
    if (toolName == "Bash")
        return QStringLiteral("\u2699");       // gear
    if (toolName == "WebFetch" || toolName == "WebSearch")
        return QStringLiteral("\u2301");       // electric arrow
    if (toolName == "Agent")
        return QStringLiteral("\u2318");       // place of interest (agent)
    if (toolName == "TodoWrite")
        return QStringLiteral("\u2611");       // ballot box with check
    if (toolName == "AskUserQuestion")
        return QStringLiteral("\u2753");       // question mark
    return QStringLiteral("\u2726");           // four-pointed star (default)
}

ToolCallGroupWidget::ToolCallGroupWidget(QWidget *parent)
    : QFrame(parent)
{
    auto &tm = ThemeManager::instance();
    setStyleSheet(
        QStringLiteral(
        "ToolCallGroupWidget { background: %1; border: 1px solid %2; "
        "border-left: 2px solid %3; border-radius: 12px; }")
        .arg(tm.hex("bg_raised"), tm.hex("border_standard"), tm.hex("text_muted")));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(10, 8, 10, 8);
    m_layout->setSpacing(0);

    // Header: expand button + summary
    m_headerLayout = new QHBoxLayout;
    m_headerLayout->setSpacing(6);

    m_expandBtn = new QPushButton(QStringLiteral("\u25B6"), this);
    m_expandBtn->setFixedSize(18, 18);
    m_expandBtn->setStyleSheet(
        QStringLiteral(
        "QPushButton { background: none; color: %1; border: none; font-size: 10px; padding: 0; }"
        "QPushButton:hover { color: %2; }")
        .arg(tm.hex("text_muted"), tm.hex("text_primary")));
    m_headerLayout->addWidget(m_expandBtn);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 11px; }")
        .arg(tm.hex("text_secondary")));
    m_summaryLabel->setWordWrap(true);
    m_headerLayout->addWidget(m_summaryLabel, 1);

    m_layout->addLayout(m_headerLayout);

    // Detail container — always in layout, height animated via maximumHeight
    m_detailContainer = new QWidget(this);
    m_detailContainer->setMaximumHeight(0);
    m_detailLayout = new QVBoxLayout(m_detailContainer);
    m_detailLayout->setContentsMargins(6, 4, 0, 2);
    m_detailLayout->setSpacing(2);
    m_layout->addWidget(m_detailContainer);

    // Height animation for smooth expand / collapse
    m_expandAnim = new QPropertyAnimation(m_detailContainer, "maximumHeight", this);
    m_expandAnim->setDuration(180);
    m_expandAnim->setEasingCurve(QEasingCurve::InOutCubic);

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ToolCallGroupWidget::applyThemeColors);

    connect(m_expandBtn, &QPushButton::clicked, this, [this] {
        m_expanded = !m_expanded;
        m_expandBtn->setText(m_expanded ? QStringLiteral("\u25BC") : QStringLiteral("\u25B6"));

        m_expandAnim->stop();
        if (m_expanded) {
            rebuildDetailView();
            // Measure natural height
            m_detailContainer->layout()->activate();
            int target = qMax(m_detailContainer->layout()->sizeHint().height(), 40);
            m_expandAnim->setStartValue(m_detailContainer->maximumHeight());
            m_expandAnim->setEndValue(target);
            // After animation, remove height cap so content can resize freely
            connect(m_expandAnim, &QPropertyAnimation::finished, this, [this] {
                if (m_expanded) m_detailContainer->setMaximumHeight(16777215);
            }, Qt::SingleShotConnection);
        } else {
            m_expandAnim->setStartValue(m_detailContainer->height());
            m_expandAnim->setEndValue(0);
        }
        m_expandAnim->start();
    });
}

void ToolCallGroupWidget::addToolCall(const ToolCallInfo &info)
{
    m_calls.append(info);
    m_toolCounts[info.toolName] = m_toolCounts.value(info.toolName, 0) + 1;
    updateSummaryLabel();
}

void ToolCallGroupWidget::finalize()
{
    updateSummaryLabel();
}

void ToolCallGroupWidget::setExpandedByDefault()
{
    m_expanded = true;
    m_expandBtn->setText(QStringLiteral("\u25BC"));
    rebuildDetailView();
    m_detailContainer->setMaximumHeight(16777215);
}

void ToolCallGroupWidget::applyThemeColors()
{
    auto &tm = ThemeManager::instance();
    setStyleSheet(
        QStringLiteral(
        "ToolCallGroupWidget { background: %1; border: 1px solid %2; "
        "border-left: 2px solid %3; border-radius: 12px; }")
        .arg(tm.hex("bg_raised"), tm.hex("border_standard"), tm.hex("text_muted")));

    m_expandBtn->setStyleSheet(
        QStringLiteral(
        "QPushButton { background: none; color: %1; border: none; font-size: 10px; padding: 0; }"
        "QPushButton:hover { color: %2; }")
        .arg(tm.hex("text_muted"), tm.hex("text_primary")));

    m_summaryLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 11px; }")
        .arg(tm.hex("text_secondary")));

    updateSummaryLabel();
}

void ToolCallGroupWidget::updateSummaryLabel()
{
    QStringList parts;
    for (auto it = m_toolCounts.constBegin(); it != m_toolCounts.constEnd(); ++it) {
        QString icon = toolIcon(it.key());
        if (it.value() > 1)
            parts << QStringLiteral("%3 <b>%1</b> x%2").arg(it.key()).arg(it.value()).arg(icon);
        else
            parts << QStringLiteral("%2 <b>%1</b>").arg(it.key(), icon);
    }

    int editCount = 0;
    for (const auto &c : m_calls)
        if (c.isEdit) editCount++;

    auto &tm = ThemeManager::instance();
    QString editNote;
    if (editCount > 0)
        editNote = QStringLiteral(" &mdash; <span style='color:%2;'>%1 file(s) modified</span>")
                       .arg(editCount).arg(tm.hex("green"));

    m_summaryLabel->setText(
        QStringLiteral("<span style='color:%4;'>%1 tool calls:</span> %2%3")
            .arg(m_calls.size())
            .arg(parts.join(", "))
            .arg(editNote)
            .arg(tm.hex("text_muted")));
}

void ToolCallGroupWidget::rebuildDetailView()
{
    // Clear old detail widgets
    QLayoutItem *item;
    while ((item = m_detailLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    auto &tm = ThemeManager::instance();
    for (const auto &call : m_calls) {
        auto *row = new QFrame(m_detailContainer);
        row->setStyleSheet(
            QStringLiteral("QFrame { background: transparent; border-radius: 4px; }"));
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(4, 2, 4, 2);
        rowLayout->setSpacing(0);

        // Tool icon + name + file path (clickable)
        QString icon = toolIcon(call.toolName);
        QString header = QStringLiteral("%3 <span style='color:%2;font-weight:bold;'>%1</span>")
                             .arg(call.toolName, tm.hex("green"), icon);
        if (!call.filePath.isEmpty())
            header += QStringLiteral(" <a href='%1' style='color:%2;text-decoration:none;'>%1</a>")
                          .arg(call.filePath.toHtmlEscaped(), tm.hex("blue"));

        auto *headerLabel = new QLabel(header, row);
        headerLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 11px; } QLabel a:hover { text-decoration: underline; }"));
        headerLabel->setTextFormat(Qt::RichText);
        headerLabel->setCursor(Qt::PointingHandCursor);
        QString searchText = call.newString.isEmpty() ? call.oldString : call.newString;
        connect(headerLabel, &QLabel::linkActivated, this, [this, searchText](const QString &fp) {
            emit fileClicked(fp, searchText);
        });
        rowLayout->addWidget(headerLabel);

        // For edit tools, show a mini diff
        if (call.isEdit && (!call.oldString.isEmpty() || !call.newString.isEmpty())) {
            auto *diffView = createDiffView(call.oldString, call.newString);
            rowLayout->addWidget(diffView);
        }

        m_detailLayout->addWidget(row);
    }
}

QWidget *ToolCallGroupWidget::createDiffView(const QString &oldStr, const QString &newStr)
{
    auto &tm = ThemeManager::instance();
    auto *browser = new QTextBrowser;
    browser->setFrameShape(QFrame::NoFrame);
    browser->setStyleSheet(
        QStringLiteral(
        "QTextBrowser { background: transparent; border: none; font-family: 'JetBrains Mono'; "
        "font-size: 12px; }"));
    browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setMaximumHeight(200);

    QString html;
    int lineCount = 0;

    // Show removed lines in red
    if (!oldStr.isEmpty()) {
        QStringList oldLines = oldStr.split('\n');
        lineCount += oldLines.size();
        for (const QString &line : oldLines) {
            QString escaped = line.toHtmlEscaped();
            html += QStringLiteral(
                "<div style='background:%2;color:%3;padding:1px 4px;'>"
                "- %1</div>").arg(escaped, tm.hex("diff_del_bg"), tm.hex("red"));
        }
    }

    // Show added lines in green
    if (!newStr.isEmpty()) {
        QStringList newLines = newStr.split('\n');
        lineCount += newLines.size();
        for (const QString &line : newLines) {
            QString escaped = line.toHtmlEscaped();
            html += QStringLiteral(
                "<div style='background:%2;color:%3;padding:1px 4px;'>"
                "+ %1</div>").arg(escaped, tm.hex("diff_add_bg"), tm.hex("green"));
        }
    }

    browser->document()->setDocumentMargin(0);
    browser->setHtml(html);

    // Size from line count: ~16px per line (12px font + 2px padding + line-height)
    int h = qMin(lineCount * 16, 200);
    browser->setFixedHeight(qMax(h, 10));

    return browser;
}
