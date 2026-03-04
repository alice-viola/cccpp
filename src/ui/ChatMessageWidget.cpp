#include "ui/ChatMessageWidget.h"
#include "ui/ThemeManager.h"
#include "util/MarkdownRenderer.h"
#include <QUrl>
#include <QUrlQuery>
#include <QDesktopServices>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTimer>
#include <QPixmap>
#include <QtMath>

ChatMessageWidget::ChatMessageWidget(Role role, const QString &content, QWidget *parent)
    : QFrame(parent)
    , m_role(role)
    , m_rawContent(content)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(6);

    auto &tm = ThemeManager::instance();

    if (role == User) {
        m_layout->setContentsMargins(14, 10, 14, 10);

        // User message content — bold directive text
        m_userLabel = new QLabel(content, this);
        m_userLabel->setWordWrap(true);
        m_userLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_userLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 13px; font-weight: 500; }")
            .arg(tm.hex("text_primary")));
        m_userLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        m_layout->addWidget(m_userLabel);

        // Footer row: timestamp (bottom right) + revert button
        m_headerWidget = new QWidget(this);
        m_headerWidget->setAttribute(Qt::WA_TranslucentBackground);
        m_headerWidget->setStyleSheet("background: transparent;");
        auto *directiveHeader = new QHBoxLayout(m_headerWidget);
        directiveHeader->setSpacing(6);
        directiveHeader->setContentsMargins(0, 2, 0, 0);

        directiveHeader->addStretch();

        m_timestampLabel = new QLabel(m_headerWidget);
        m_timestampLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 10px; }").arg(tm.hex("text_faint")));
        directiveHeader->addWidget(m_timestampLabel);

        m_revertBtn = new QPushButton("Revert", m_headerWidget);
        m_revertBtn->setFixedHeight(20);
        m_revertBtn->setStyleSheet(
            QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 4px; font-size: 10px; padding: 0 8px; }"
            "QPushButton:hover { background: %3; color: %4; }")
            .arg(tm.hex("bg_raised"), tm.hex("text_muted"),
                 tm.hex("border_standard"), tm.hex("text_primary")));
        m_revertBtn->setVisible(false);
        connect(m_revertBtn, &QPushButton::clicked, this, [this] { emit revertRequested(m_turnId); });
        directiveHeader->addWidget(m_revertBtn);

        m_layout->addWidget(m_headerWidget);

        m_roleLabel = nullptr;

        m_acceptBtn = nullptr;
        m_rejectBtn = nullptr;
    } else {
        m_layout->setContentsMargins(16, 14, 16, 14);

        m_headerWidget = new QWidget(this);
        auto *headerLayout = new QHBoxLayout(m_headerWidget);
        headerLayout->setSpacing(4);
        headerLayout->setContentsMargins(0, 0, 0, 0);

        QString roleName, roleColor;
        switch (role) {
        case Assistant: roleName = "Claude"; roleColor = tm.hex("text_secondary"); break;
        case Tool:     roleName = "Tool";   roleColor = tm.hex("green"); break;
        default:       roleName = ""; roleColor = tm.hex("text_primary"); break;
        }

        m_roleLabel = new QLabel(roleName, m_headerWidget);
        m_roleLabel->setStyleSheet(
            QStringLiteral("QLabel { font-weight: bold; font-size: 12px; color: %1; }").arg(roleColor));
        headerLayout->addWidget(m_roleLabel);

        m_timestampLabel = new QLabel(m_headerWidget);
        m_timestampLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 10px; margin-left: 6px; }").arg(tm.hex("text_faint")));
        headerLayout->addWidget(m_timestampLabel);

        headerLayout->addStretch();

        m_acceptBtn = new QPushButton("Accept", m_headerWidget);
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

        m_rejectBtn = new QPushButton("Reject", m_headerWidget);
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

        m_revertBtn = new QPushButton("Revert", m_headerWidget);
        m_revertBtn->setFixedHeight(20);
        m_revertBtn->setStyleSheet(
            QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 4px; font-size: 11px; padding: 0 8px; }"
            "QPushButton:hover { background: %3; color: %4; }")
            .arg(tm.hex("bg_raised"), tm.hex("text_muted"),
                 tm.hex("border_standard"), tm.hex("text_primary")));
        m_revertBtn->setVisible(false);
        connect(m_revertBtn, &QPushButton::clicked, this, [this] { emit revertRequested(m_turnId); });
        headerLayout->addWidget(m_revertBtn);

        m_layout->addWidget(m_headerWidget);

        setupAssistantContent(content);
    }

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    applyStyle();
    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ChatMessageWidget::applyThemeColors);
}

void ChatMessageWidget::setImages(const QList<QByteArray> &imageDataList)
{
    if (imageDataList.isEmpty()) return;

    m_imageContainer = new QWidget(this);
    auto *imgLayout = new QHBoxLayout(m_imageContainer);
    imgLayout->setContentsMargins(0, 0, 0, 4);
    imgLayout->setSpacing(6);

    auto &p = ThemeManager::instance().palette();
    for (const auto &data : imageDataList) {
        QPixmap px;
        px.loadFromData(data);
        if (px.isNull()) continue;

        auto *frame = new QFrame(m_imageContainer);
        frame->setStyleSheet(QStringLiteral(
            "QFrame { background: %1; border: 1px solid %2; border-radius: 8px; padding: 2px; }")
            .arg(p.bg_raised.name(), p.border_standard.name()));
        auto *frameLayout = new QVBoxLayout(frame);
        frameLayout->setContentsMargins(2, 2, 2, 2);

        auto *label = new QLabel(frame);
        label->setPixmap(px.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        frameLayout->addWidget(label);
        imgLayout->addWidget(frame);
    }
    imgLayout->addStretch();

    m_layout->insertWidget(0, m_imageContainer);
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
    m_contentBrowser->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard | Qt::LinksAccessibleByMouse);

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
        } else if (url.scheme() == "cccpp" && url.host() == "copy") {
            QUrlQuery q(url);
            int blockIdx = q.queryItemValue("block").toInt();
            // Extract code from raw content for the given block index
            QString raw = m_rawContent;
            const auto &fenced = MarkdownRenderer::fencedCodeRegex();
            auto it = fenced.globalMatch(raw);
            int idx = 0;
            while (it.hasNext()) {
                auto match = it.next();
                if (idx == blockIdx) {
                    QApplication::clipboard()->setText(match.captured(2));
                    break;
                }
                ++idx;
            }
        } else if (url.scheme() == "cccpp" && url.host() == "apply") {
            QUrlQuery q(url);
            int blockIdx = q.queryItemValue("block").toInt();
            QString lang = q.queryItemValue("lang");
            QString raw = m_rawContent;
            const auto &fenced = MarkdownRenderer::fencedCodeRegex();
            auto it = fenced.globalMatch(raw);
            int idx = 0;
            while (it.hasNext()) {
                auto match = it.next();
                if (idx == blockIdx) {
                    emit applyCodeRequested(match.captured(2), lang);
                    break;
                }
                ++idx;
            }
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
        QString html = renderer.toHtml(m_rawContent);
        if (!m_pendingHtmlBlocks.isEmpty())
            html += m_pendingHtmlBlocks.join("");
        m_contentBrowser->setHtml(html);
    } else if (m_userLabel) {
        m_userLabel->setText(m_rawContent);
    }
}

void ChatMessageWidget::appendRawHtml(const QString &html, const QString &plainSummary)
{
    m_rawContent += plainSummary;
    m_pendingHtmlBlocks.append(html);
    if (m_contentBrowser) {
        MarkdownRenderer renderer;
        QString rendered = renderer.toHtml(m_rawContent);
        rendered += m_pendingHtmlBlocks.join("");
        m_contentBrowser->setHtml(rendered);
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
        QStringLiteral("QLabel { font-weight: bold; font-size: 12px; color: %1; }")
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
        "QPushButton { background: none; color: %1; border: none; font-size: 10px; padding: 0; }"
        "QPushButton:hover { color: %2; }")
        .arg(tm.hex("text_muted"), tm.hex("text_primary")));
    summaryLayout->addWidget(m_expandBtn);

    auto *summaryLabel = new QLabel(summary, this);
    summaryLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 11px; font-family: 'JetBrains Mono'; }")
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
        "font-family: 'JetBrains Mono'; font-size: 11px; padding: 4px; }")
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

void ChatMessageWidget::setTimestamp(const QDateTime &dt)
{
    if (m_timestampLabel)
        m_timestampLabel->setText(dt.toString("hh:mm"));
}

void ChatMessageWidget::showRevertButton(bool show)
{
    if (m_revertBtn) m_revertBtn->setVisible(show);
}

void ChatMessageWidget::showAcceptRejectButtons(bool show)
{
    if (m_acceptBtn) m_acceptBtn->setVisible(show);
    if (m_rejectBtn) m_rejectBtn->setVisible(show);
}

void ChatMessageWidget::setReverted(bool reverted)
{
    if (reverted) {
        setStyleSheet(
            QStringLiteral("ChatMessageWidget { background: %1; border-radius: 8px; }")
            .arg(ThemeManager::instance().hex("bg_base")));
        if (m_revertBtn) {
            m_revertBtn->setEnabled(false);
            m_revertBtn->setText("Reverted");
        }
    }
}

void ChatMessageWidget::applyStyle()
{
    auto &tm = ThemeManager::instance();
    switch (m_role) {
    case User:
        // Directive card: raised background with mauve left accent
        setStyleSheet(
            QStringLiteral(
            "ChatMessageWidget { background: %1; border: 1px solid %2; "
            "border-left: 3px solid %3; border-radius: 8px; }")
            .arg(tm.hex("bg_raised"), tm.hex("border_standard"), tm.hex("mauve")));
        break;
    case Assistant:
        setStyleSheet(
            QStringLiteral("ChatMessageWidget { background: transparent; }"));
        break;
    case Tool:
        setStyleSheet(
            QStringLiteral("ChatMessageWidget { background: transparent; }"));
        break;
    }
}

void ChatMessageWidget::applyThemeColors()
{
    auto &tm = ThemeManager::instance();

    applyStyle();

    QString roleColor;
    switch (m_role) {
    case User:     roleColor = tm.hex("text_primary"); break;
    case Assistant: roleColor = tm.hex("text_secondary"); break;
    case Tool:     roleColor = tm.hex("green"); break;
    }
    if (m_roleLabel)
        m_roleLabel->setStyleSheet(
            QStringLiteral("QLabel { font-weight: bold; font-size: 12px; color: %1; }").arg(roleColor));

    if (m_userLabel)
        m_userLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 13px; }")
            .arg(tm.hex("text_primary")));

    if (m_acceptBtn)
        m_acceptBtn->setStyleSheet(
            QStringLiteral(
            "QPushButton { background: %1; color: %2; border: none; "
            "border-radius: 4px; font-size: 11px; font-weight: bold; padding: 0 8px; }"
            "QPushButton:hover { background: %3; }")
            .arg(tm.hex("green"), tm.hex("on_accent"), tm.hex("teal")));

    if (m_rejectBtn)
        m_rejectBtn->setStyleSheet(
            QStringLiteral(
            "QPushButton { background: %1; color: %2; border: none; "
            "border-radius: 4px; font-size: 11px; font-weight: bold; padding: 0 8px; }"
            "QPushButton:hover { background: %3; }")
            .arg(tm.hex("red"), tm.hex("on_accent"), tm.hex("maroon")));

    if (m_revertBtn)
        m_revertBtn->setStyleSheet(
            QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 4px; font-size: 11px; padding: 0 8px; }"
            "QPushButton:hover { background: %3; color: %4; }")
            .arg(tm.hex("bg_raised"), tm.hex("text_muted"),
                 tm.hex("border_standard"), tm.hex("text_primary")));

    if (m_contentBrowser && !m_rawContent.isEmpty()) {
        MarkdownRenderer renderer;
        QString html = renderer.toHtml(m_rawContent);
        if (!m_pendingHtmlBlocks.isEmpty())
            html += m_pendingHtmlBlocks.join("");
        m_contentBrowser->setHtml(html);
    }
}
