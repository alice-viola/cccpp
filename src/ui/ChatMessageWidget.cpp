#include "ui/ChatMessageWidget.h"
#include "util/MarkdownRenderer.h"
#include <QUrl>
#include <QUrlQuery>
#include <QDesktopServices>
#include <QTimer>

ChatMessageWidget::ChatMessageWidget(Role role, const QString &content, QWidget *parent)
    : QFrame(parent)
    , m_role(role)
    , m_rawContent(content)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 3, 8, 3);
    m_layout->setSpacing(0);

    // Header: role label + buttons
    auto *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(4);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    QString roleName, roleColor;
    switch (role) {
    case User:     roleName = "You";    roleColor = "#cba6f7"; break;
    case Assistant: roleName = "Claude"; roleColor = "#89b4fa"; break;
    case Tool:     roleName = "Tool";   roleColor = "#a6e3a1"; break;
    }

    m_roleLabel = new QLabel(roleName, this);
    m_roleLabel->setStyleSheet(
        QStringLiteral("QLabel { font-weight: bold; font-size: 11px; color: %1; }").arg(roleColor));
    headerLayout->addWidget(m_roleLabel);
    headerLayout->addStretch();

    m_acceptBtn = new QPushButton("Accept", this);
    m_acceptBtn->setFixedHeight(20);
    m_acceptBtn->setStyleSheet(
        "QPushButton { background: #a6e3a1; color: #0e0e0e; border: none; "
        "border-radius: 4px; font-size: 11px; font-weight: bold; padding: 0 8px; }"
        "QPushButton:hover { background: #b6f0b1; }");
    m_acceptBtn->setVisible(false);
    connect(m_acceptBtn, &QPushButton::clicked, this, [this] { emit acceptRequested(m_turnId); });
    headerLayout->addWidget(m_acceptBtn);

    m_rejectBtn = new QPushButton("Reject", this);
    m_rejectBtn->setFixedHeight(20);
    m_rejectBtn->setStyleSheet(
        "QPushButton { background: #f38ba8; color: #0e0e0e; border: none; "
        "border-radius: 4px; font-size: 11px; font-weight: bold; padding: 0 8px; }"
        "QPushButton:hover { background: #f5a0b6; }");
    m_rejectBtn->setVisible(false);
    connect(m_rejectBtn, &QPushButton::clicked, this, [this] { emit rejectRequested(m_turnId); });
    headerLayout->addWidget(m_rejectBtn);

    m_revertBtn = new QPushButton("Revert", this);
    m_revertBtn->setFixedHeight(20);
    m_revertBtn->setStyleSheet(
        "QPushButton { background: #252525; color: #f38ba8; border: none; "
        "border-radius: 4px; font-size: 11px; padding: 0 8px; }"
        "QPushButton:hover { background: #f38ba8; color: #0e0e0e; }");
    m_revertBtn->setVisible(false);
    connect(m_revertBtn, &QPushButton::clicked, this, [this] { emit revertRequested(m_turnId); });
    headerLayout->addWidget(m_revertBtn);

    m_layout->addLayout(headerLayout);

    // Content depends on role
    if (role == User) {
        setupUserContent(content);
    } else {
        setupAssistantContent(content);
    }

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    applyStyle();
}

void ChatMessageWidget::setupUserContent(const QString &content)
{
    m_userLabel = new QLabel(content, this);
    m_userLabel->setWordWrap(true);
    m_userLabel->setStyleSheet(
        "QLabel { color: #cdd6f4; font-size: 13px; padding: 2px 0; }");
    m_userLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_layout->addWidget(m_userLabel);
}

void ChatMessageWidget::setupAssistantContent(const QString &content)
{
    m_contentBrowser = new QTextBrowser(this);
    m_contentBrowser->setOpenExternalLinks(false);
    m_contentBrowser->setOpenLinks(false);
    m_contentBrowser->setFrameShape(QFrame::NoFrame);
    m_contentBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentBrowser->document()->setDocumentMargin(0);
    m_contentBrowser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    if (!content.isEmpty()) {
        MarkdownRenderer renderer;
        m_contentBrowser->setHtml(renderer.toHtml(content));
    } else {
        // Start collapsed — will grow when content arrives
        m_contentBrowser->setMaximumHeight(0);
        m_contentBrowser->setMinimumHeight(0);
    }

    connect(m_contentBrowser->document(), &QTextDocument::contentsChanged,
            this, &ChatMessageWidget::resizeBrowser);

    connect(m_contentBrowser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == "cccpp" && url.host() == "open") {
            QString file = QUrlQuery(url).queryItemValue("file");
            if (!file.isEmpty())
                emit fileNavigationRequested(file);
        } else if (url.scheme() == "http" || url.scheme() == "https") {
            QDesktopServices::openUrl(url);
        }
    });

    m_layout->addWidget(m_contentBrowser);
}

void ChatMessageWidget::resizeBrowser()
{
    if (!m_contentBrowser) return;
    QTimer::singleShot(0, this, [this] {
        if (!m_contentBrowser) return;
        int vpWidth = m_contentBrowser->viewport()->width();
        if (vpWidth > 0)
            m_contentBrowser->document()->setTextWidth(vpWidth);
        int h = static_cast<int>(m_contentBrowser->document()->size().height());
        m_contentBrowser->setMinimumHeight(qMax(h, 14));
        m_contentBrowser->setMaximumHeight(qMax(h, 14));
    });
}

void ChatMessageWidget::appendContent(const QString &text)
{
    m_rawContent += text;
    if (m_contentBrowser) {
        MarkdownRenderer renderer;
        m_contentBrowser->setHtml(renderer.toHtml(m_rawContent));
    } else if (m_userLabel) {
        m_userLabel->setText(m_rawContent);
    }
}

void ChatMessageWidget::appendHtmlOnly(const QString &html, const QString &plainTextForStorage)
{
    // Append plain text to rawContent (for DB storage) but render with extra HTML
    m_rawContent += plainTextForStorage;
    if (m_contentBrowser) {
        MarkdownRenderer renderer;
        // Render the markdown portion, then append raw HTML
        m_contentBrowser->setHtml(renderer.toHtml(m_rawContent) + html);
    }
}

void ChatMessageWidget::setToolInfo(const QString &toolName, const QString &summary)
{
    m_roleLabel->setText(QStringLiteral("Tool: %1").arg(toolName));
    m_roleLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 11px; color: #a6e3a1; }");
    setupToolWidget(toolName, summary);
}

void ChatMessageWidget::setupToolWidget(const QString &, const QString &summary)
{
    if (m_contentBrowser) m_contentBrowser->setVisible(false);
    if (m_userLabel) m_userLabel->setVisible(false);

    auto *summaryLayout = new QHBoxLayout;
    summaryLayout->setSpacing(4);
    summaryLayout->setContentsMargins(0, 0, 0, 0);

    m_expandBtn = new QPushButton(QStringLiteral("\u25B6"), this);
    m_expandBtn->setFixedSize(16, 16);
    m_expandBtn->setStyleSheet(
        "QPushButton { background: none; color: #6c7086; border: none; font-size: 9px; padding: 0; }"
        "QPushButton:hover { color: #cdd6f4; }");
    summaryLayout->addWidget(m_expandBtn);

    auto *summaryLabel = new QLabel(summary, this);
    summaryLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 11px; font-family: Menlo, monospace; }");
    summaryLabel->setWordWrap(true);
    summaryLayout->addWidget(summaryLabel, 1);

    m_layout->addLayout(summaryLayout);

    m_toolDetailWidget = new QWidget(this);
    m_toolDetailWidget->setVisible(false);
    auto *detailLayout = new QVBoxLayout(m_toolDetailWidget);
    detailLayout->setContentsMargins(18, 2, 0, 2);

    auto *detailBrowser = new QTextBrowser(m_toolDetailWidget);
    detailBrowser->setFrameShape(QFrame::NoFrame);
    detailBrowser->setStyleSheet(
        "QTextBrowser { background: #0e0e0e; color: #a6adc8; border: none; "
        "font-family: Menlo, monospace; font-size: 11px; padding: 4px; }");
    detailBrowser->setMaximumHeight(120);
    detailBrowser->setPlainText(summary);
    detailLayout->addWidget(detailBrowser);

    m_layout->addWidget(m_toolDetailWidget);

    connect(m_expandBtn, &QPushButton::clicked, this, [this] {
        m_isCollapsed = !m_isCollapsed;
        m_toolDetailWidget->setVisible(!m_isCollapsed);
        m_expandBtn->setText(m_isCollapsed ? QStringLiteral("\u25B6") : QStringLiteral("\u25BC"));
    });
}

void ChatMessageWidget::showRevertButton(bool show) { m_revertBtn->setVisible(show); }
void ChatMessageWidget::showAcceptRejectButtons(bool show) {
    m_acceptBtn->setVisible(show);
    m_rejectBtn->setVisible(show);
}

void ChatMessageWidget::setReverted(bool reverted)
{
    if (reverted) {
        setStyleSheet("ChatMessageWidget { background: #0e0e0e; border-radius: 6px; }");
        m_revertBtn->setEnabled(false);
        m_revertBtn->setText("Reverted");
    }
}

void ChatMessageWidget::applyStyle()
{
    switch (m_role) {
    case User:
        setStyleSheet(
            "ChatMessageWidget { background: #141414; border: 1px solid #2a2a2a; "
            "border-radius: 6px; }");
        break;
    case Assistant:
        setStyleSheet(
            "ChatMessageWidget { background: #0e0e0e; border-left: 2px solid #89b4fa; "
            "border-radius: 0; }");
        break;
    case Tool:
        setStyleSheet(
            "ChatMessageWidget { background: transparent; border-radius: 0; }");
        break;
    }
}
