#include "ui/ToolCallGroupWidget.h"
#include "ui/ThemeManager.h"
#include <QScrollArea>

ToolCallGroupWidget::ToolCallGroupWidget(QWidget *parent)
    : QFrame(parent)
{
    auto &tm = ThemeManager::instance();
    setStyleSheet(
        QStringLiteral(
        "ToolCallGroupWidget { background: %1; border: 1px solid %2; "
        "border-left: 2px solid %3; border-radius: 6px; }")
        .arg(tm.hex("bg_surface"), tm.hex("border_standard"), tm.hex("green")));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 6, 8, 6);
    m_layout->setSpacing(0);

    // Header: expand button + summary
    m_headerLayout = new QHBoxLayout;
    m_headerLayout->setSpacing(6);

    m_expandBtn = new QPushButton(QStringLiteral("\u25B6"), this);
    m_expandBtn->setFixedSize(18, 18);
    m_expandBtn->setStyleSheet(
        QStringLiteral(
        "QPushButton { background: none; color: %1; border: none; font-size: 9px; padding: 0; }"
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
    m_detailLayout->setContentsMargins(24, 6, 0, 4);
    m_detailLayout->setSpacing(4);
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

void ToolCallGroupWidget::applyThemeColors()
{
    auto &tm = ThemeManager::instance();
    setStyleSheet(
        QStringLiteral(
        "ToolCallGroupWidget { background: %1; border: 1px solid %2; "
        "border-left: 2px solid %3; border-radius: 6px; }")
        .arg(tm.hex("bg_surface"), tm.hex("border_standard"), tm.hex("green")));

    m_expandBtn->setStyleSheet(
        QStringLiteral(
        "QPushButton { background: none; color: %1; border: none; font-size: 9px; padding: 0; }"
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
        if (it.value() > 1)
            parts << QStringLiteral("<b>%1</b> x%2").arg(it.key()).arg(it.value());
        else
            parts << QStringLiteral("<b>%1</b>").arg(it.key());
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
            QStringLiteral("QFrame { background: %1; border-radius: 4px; }")
            .arg(tm.hex("bg_base")));
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(8, 4, 8, 4);
        rowLayout->setSpacing(2);

        // Tool name + file path
        QString header = QStringLiteral("<span style='color:%2;font-weight:bold;'>%1</span>")
                             .arg(call.toolName, tm.hex("green"));
        if (!call.filePath.isEmpty())
            header += QStringLiteral(" <span style='color:%2;'>%1</span>").arg(call.filePath, tm.hex("blue"));

        auto *headerLabel = new QLabel(header, row);
        headerLabel->setStyleSheet("QLabel { font-size: 11px; }");
        headerLabel->setTextFormat(Qt::RichText);
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
        "QTextBrowser { background: %1; border: none; font-family: Menlo, monospace; "
        "font-size: 12px; }")
        .arg(tm.hex("bg_base")));
    browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setMaximumHeight(200);

    QString html;

    // Show removed lines in red
    if (!oldStr.isEmpty()) {
        QStringList oldLines = oldStr.split('\n');
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
        for (const QString &line : newLines) {
            QString escaped = line.toHtmlEscaped();
            html += QStringLiteral(
                "<div style='background:%2;color:%3;padding:1px 4px;'>"
                "+ %1</div>").arg(escaped, tm.hex("diff_add_bg"), tm.hex("green"));
        }
    }

    browser->setHtml(html);

    // Auto-size
    connect(browser->document(), &QTextDocument::contentsChanged, browser, [browser] {
        int h = static_cast<int>(browser->document()->size().height()) + 4;
        browser->setFixedHeight(qMin(h, 200));
    });

    return browser;
}
