#include "ui/ChatMessageWidget.h"
#include "ui/ThemeManager.h"
#include "util/MarkdownRenderer.h"
#include <QUrl>
#include <QUrlQuery>
#include <QDesktopServices>
#include <QResizeEvent>
#include <QTimer>
#include <QtMath>

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

    auto &tm = ThemeManager::instance();
    QString roleName, roleColor;
    switch (role) {
    case User:     roleName = "You";    roleColor = tm.hex("mauve"); break;
    case Assistant: roleName = "Claude"; roleColor = tm.hex("blue"); break;
    case Tool:     roleName = "Tool";   roleColor = tm.hex("green"); break;
    }

    m_roleLabel = new QLabel(roleName, this);
    m_roleLabel->setStyleSheet(
        QStringLiteral("QLabel { font-weight: bold; font-size: 11px; color: %1; }").arg(roleColor));
    headerLayout->addWidget(m_roleLabel);
    headerLayout->addStretch();

    m_acceptBtn = new QPushButton("Accept", this);
    m_acceptBtn->setFixedHeight(20);
    m_acceptBtn->setStyleSheet(
        QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; "
        "border-radius: 4px; font-size: 11px; font-weight: bold; padding: 0 8px; }"
        "QPushButton:hover { background: %3; }")
        .arg(tm.hex("green"), tm.hex("on_accent"), tm.hex("teal")));
    m_acceptBtn->setVisible(false);
    connect(m_acceptBtn, &QPushButton::clicked, this, [this] { emit acceptRequested(m_turnId); });
    headerLayout->addWidget(m_acceptBtn);

    m_rejectBtn = new QPushButton("Reject", this);
    m_rejectBtn->setFixedHeight(20);
    m_rejectBtn->setStyleSheet(
        QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; "
        "border-radius: 4px; font-size: 11px; font-weight: bold; padding: 0 8px; }"
        "QPushButton:hover { background: %3; }")
        .arg(tm.hex("red"), tm.hex("on_accent"), tm.hex("maroon")));
    m_rejectBtn->setVisible(false);
    connect(m_rejectBtn, &QPushButton::clicked, this, [this] { emit rejectRequested(m_turnId); });
    headerLayout->addWidget(m_rejectBtn);

    m_revertBtn = new QPushButton("Revert", this);
    m_revertBtn->setFixedHeight(20);
    m_revertBtn->setStyleSheet(
        QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; "
        "border-radius: 4px; font-size: 11px; padding: 0 8px; }"
        "QPushButton:hover { background: %2; color: %3; }")
        .arg(tm.hex("bg_raised"), tm.hex("red"), tm.hex("on_accent")));
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
        QStringLiteral("QLabel { color: %1; font-size: 13px; padding: 2px 0; }")
        .arg(ThemeManager::instance().hex("text_primary")));
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

    connect(m_contentBrowser->document(), &QTextDocument::contentsChanged,
            this, &ChatMessageWidget::resizeBrowser);

    if (!content.isEmpty()) {
        MarkdownRenderer renderer;
        m_contentBrowser->setHtml(renderer.toHtml(content));
    } else {
        m_contentBrowser->setMaximumHeight(0);
        m_contentBrowser->setMinimumHeight(0);
    }

    connect(m_contentBrowser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == "cccpp" && url.host() == "open") {
            QUrlQuery q(url);
            QString file = q.queryItemValue("file");
            int line = q.queryItemValue("line").toInt();
            if (!file.isEmpty())
                emit fileNavigationRequested(file, line);
        } else if (url.scheme() == "http" || url.scheme() == "https") {
            QDesktopServices::openUrl(url);
        }
    });

    m_layout->addWidget(m_contentBrowser);
}

void ChatMessageWidget::resizeBrowser()
{
    if (!m_contentBrowser) return;
    if (m_resizePending) return;
    m_resizePending = true;
    QTimer::singleShot(0, this, [this] {
        m_resizePending = false;
        if (!m_contentBrowser) return;
        int vpWidth = m_contentBrowser->viewport()->width();
        if (vpWidth <= 0)
            vpWidth = m_contentBrowser->width() - 4;
        if (vpWidth <= 0) {
            // Widget not laid out yet — retry after layout settles
            QTimer::singleShot(50, this, [this] { resizeBrowser(); });
            return;
        }
        m_contentBrowser->document()->setTextWidth(vpWidth);
        int h = qCeil(m_contentBrowser->document()->size().height()) + 2;
        m_contentBrowser->setMinimumHeight(qMax(h, 14));
        m_contentBrowser->setMaximumHeight(qMax(h, 14));
    });
}

void ChatMessageWidget::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    resizeBrowser();
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
    m_roleLabel->setStyleSheet(
        QStringLiteral("QLabel { font-weight: bold; font-size: 11px; color: %1; }")
        .arg(ThemeManager::instance().hex("green")));
    setupToolWidget(toolName, summary);
}

void ChatMessageWidget::setupToolWidget(const QString &, const QString &summary)
{
    if (m_contentBrowser) m_contentBrowser->setVisible(false);
    if (m_userLabel) m_userLabel->setVisible(false);

    auto *summaryLayout = new QHBoxLayout;
    summaryLayout->setSpacing(4);
    summaryLayout->setContentsMargins(0, 0, 0, 0);

    auto &tm = ThemeManager::instance();
    m_expandBtn = new QPushButton(QStringLiteral("\u25B6"), this);
    m_expandBtn->setFixedSize(16, 16);
    m_expandBtn->setStyleSheet(
        QStringLiteral(
        "QPushButton { background: none; color: %1; border: none; font-size: 9px; padding: 0; }"
        "QPushButton:hover { color: %2; }")
        .arg(tm.hex("text_muted"), tm.hex("text_primary")));
    summaryLayout->addWidget(m_expandBtn);

    auto *summaryLabel = new QLabel(summary, this);
    summaryLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 11px; font-family: Menlo, monospace; }")
        .arg(tm.hex("text_muted")));
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
        QStringLiteral(
        "QTextBrowser { background: %1; color: %2; border: none; "
        "font-family: Menlo, monospace; font-size: 11px; padding: 4px; }")
        .arg(tm.hex("bg_base"), tm.hex("text_secondary")));
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
        setStyleSheet(
            QStringLiteral("ChatMessageWidget { background: %1; border-radius: 6px; }")
            .arg(ThemeManager::instance().hex("bg_base")));
        m_revertBtn->setEnabled(false);
        m_revertBtn->setText("Reverted");
    }
}

void ChatMessageWidget::applyStyle()
{
    auto &tm = ThemeManager::instance();
    switch (m_role) {
    case User:
        setStyleSheet(
            QStringLiteral(
            "ChatMessageWidget { background: %1; border: 1px solid %2; "
            "border-radius: 6px; }")
            .arg(tm.hex("bg_surface"), tm.hex("border_standard")));
        break;
    case Assistant:
        setStyleSheet(
            QStringLiteral(
            "ChatMessageWidget { background: %1; border-left: 2px solid %2; "
            "border-radius: 0; }")
            .arg(tm.hex("bg_base"), tm.hex("blue")));
        break;
    case Tool:
        setStyleSheet(
            "ChatMessageWidget { background: transparent; border-radius: 0; }");
        break;
    }
}
