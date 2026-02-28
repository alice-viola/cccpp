#include "ui/ToolCallGroupWidget.h"
#include <QScrollArea>

ToolCallGroupWidget::ToolCallGroupWidget(QWidget *parent)
    : QFrame(parent)
{
    setStyleSheet(
        "ToolCallGroupWidget { background: #141414; border: 1px solid #2a2a2a; "
        "border-radius: 6px; }");

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 6, 8, 6);
    m_layout->setSpacing(0);

    // Header: expand button + summary
    m_headerLayout = new QHBoxLayout;
    m_headerLayout->setSpacing(6);

    m_expandBtn = new QPushButton(QStringLiteral("\u25B6"), this);
    m_expandBtn->setFixedSize(18, 18);
    m_expandBtn->setStyleSheet(
        "QPushButton { background: none; color: #6c7086; border: none; font-size: 9px; padding: 0; }"
        "QPushButton:hover { color: #cdd6f4; }");
    m_headerLayout->addWidget(m_expandBtn);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setStyleSheet(
        "QLabel { color: #a6adc8; font-size: 11px; }");
    m_summaryLabel->setWordWrap(true);
    m_headerLayout->addWidget(m_summaryLabel, 1);

    m_layout->addLayout(m_headerLayout);

    // Detail container (hidden by default)
    m_detailContainer = new QWidget(this);
    m_detailContainer->setVisible(false);
    m_detailLayout = new QVBoxLayout(m_detailContainer);
    m_detailLayout->setContentsMargins(24, 6, 0, 4);
    m_detailLayout->setSpacing(4);
    m_layout->addWidget(m_detailContainer);

    connect(m_expandBtn, &QPushButton::clicked, this, [this] {
        m_expanded = !m_expanded;
        m_detailContainer->setVisible(m_expanded);
        m_expandBtn->setText(m_expanded ? QStringLiteral("\u25BC") : QStringLiteral("\u25B6"));
        if (m_expanded)
            rebuildDetailView();
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

    QString editNote;
    if (editCount > 0)
        editNote = QStringLiteral(" &mdash; <span style='color:#a6e3a1;'>%1 file(s) modified</span>")
                       .arg(editCount);

    m_summaryLabel->setText(
        QStringLiteral("<span style='color:#6c7086;'>%1 tool calls:</span> %2%3")
            .arg(m_calls.size())
            .arg(parts.join(", "))
            .arg(editNote));
}

void ToolCallGroupWidget::rebuildDetailView()
{
    // Clear old detail widgets
    QLayoutItem *item;
    while ((item = m_detailLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    for (const auto &call : m_calls) {
        auto *row = new QFrame(m_detailContainer);
        row->setStyleSheet("QFrame { background: #0e0e0e; border-radius: 4px; }");
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(8, 4, 8, 4);
        rowLayout->setSpacing(2);

        // Tool name + file path
        QString header = QStringLiteral("<span style='color:#a6e3a1;font-weight:bold;'>%1</span>")
                             .arg(call.toolName);
        if (!call.filePath.isEmpty())
            header += QStringLiteral(" <span style='color:#89b4fa;'>%1</span>").arg(call.filePath);

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
    auto *browser = new QTextBrowser;
    browser->setFrameShape(QFrame::NoFrame);
    browser->setStyleSheet(
        "QTextBrowser { background: #0e0e0e; border: none; font-family: Menlo, monospace; "
        "font-size: 12px; }");
    browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setMaximumHeight(200);

    QString html;

    // Show removed lines in red
    if (!oldStr.isEmpty()) {
        QStringList oldLines = oldStr.split('\n');
        for (const QString &line : oldLines) {
            QString escaped = line.toHtmlEscaped();
            html += QStringLiteral(
                "<div style='background:#2e1a1e;color:#f38ba8;padding:1px 4px;'>"
                "- %1</div>").arg(escaped);
        }
    }

    // Show added lines in green
    if (!newStr.isEmpty()) {
        QStringList newLines = newStr.split('\n');
        for (const QString &line : newLines) {
            QString escaped = line.toHtmlEscaped();
            html += QStringLiteral(
                "<div style='background:#1a2e1a;color:#a6e3a1;padding:1px 4px;'>"
                "+ %1</div>").arg(escaped);
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
