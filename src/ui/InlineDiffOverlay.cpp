#include "ui/InlineDiffOverlay.h"
#include "ui/ThemeManager.h"
#include <QScrollArea>
#include <QFileInfo>

InlineDiffOverlay::InlineDiffOverlay(QWidget *parent)
    : QFrame(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Header bar with Accept All / Reject All
    auto *headerWidget = new QWidget(this);
    m_headerLayout = new QHBoxLayout(headerWidget);
    m_headerLayout->setContentsMargins(8, 4, 8, 4);
    m_headerLayout->setSpacing(6);

    m_titleLabel = new QLabel("Pending changes", this);
    m_headerLayout->addWidget(m_titleLabel);
    m_headerLayout->addStretch();

    m_acceptAllBtn = new QPushButton("Accept All", this);
    m_acceptAllBtn->setFixedHeight(22);
    connect(m_acceptAllBtn, &QPushButton::clicked, this, &InlineDiffOverlay::acceptAll);
    m_headerLayout->addWidget(m_acceptAllBtn);

    m_rejectAllBtn = new QPushButton("Reject All", this);
    m_rejectAllBtn->setFixedHeight(22);
    connect(m_rejectAllBtn, &QPushButton::clicked, this, &InlineDiffOverlay::rejectAll);
    m_headerLayout->addWidget(m_rejectAllBtn);

    m_closeBtn = new QPushButton("\xc3\x97", this);
    m_closeBtn->setFixedSize(22, 22);
    connect(m_closeBtn, &QPushButton::clicked, this, &InlineDiffOverlay::closed);
    m_headerLayout->addWidget(m_closeBtn);

    m_layout->addWidget(headerWidget);

    // Scrollable diff content
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMaximumHeight(300);

    m_diffContent = new QWidget;
    m_diffLayout = new QVBoxLayout(m_diffContent);
    m_diffLayout->setContentsMargins(4, 4, 4, 4);
    m_diffLayout->setSpacing(4);
    m_diffLayout->addStretch();

    scrollArea->setWidget(m_diffContent);
    m_layout->addWidget(scrollArea);

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &InlineDiffOverlay::applyThemeColors);
}

void InlineDiffOverlay::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();

    setStyleSheet(QStringLiteral(
        "InlineDiffOverlay { background: %1; border: 1px solid %2; border-radius: 6px; }")
        .arg(p.bg_surface.name(), p.border_standard.name()));

    m_titleLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; font-weight: bold; }")
        .arg(p.text_primary.name()));

    m_acceptAllBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
        "font-size: 11px; font-weight: bold; padding: 0 10px; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.green.name(), p.on_accent.name(), p.teal.name()));

    m_rejectAllBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
        "font-size: 11px; font-weight: bold; padding: 0 10px; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.red.name(), p.on_accent.name(), p.maroon.name()));

    m_closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; font-size: 14px; }"
        "QPushButton:hover { color: %2; }")
        .arg(p.text_muted.name(), p.text_primary.name()));

    rebuild();
}

void InlineDiffOverlay::setFilePath(const QString &path)
{
    m_filePath = path;
    QFileInfo fi(path);
    m_titleLabel->setText(QStringLiteral("Changes in %1").arg(fi.fileName()));
}

void InlineDiffOverlay::setDiff(const QString &oldText, const QString &newText, int startLine)
{
    DiffHunkData hunk;
    hunk.startLine = startLine;
    hunk.oldText = oldText;
    hunk.newText = newText;
    hunk.oldLineCount = oldText.split('\n').size();
    hunk.newLineCount = newText.split('\n').size();
    hunk.filePath = m_filePath;
    m_hunks.append(hunk);

    m_titleLabel->setText(QStringLiteral("Changes in %1 (%2 edit%3)")
        .arg(QFileInfo(m_filePath).fileName())
        .arg(m_hunks.size())
        .arg(m_hunks.size() > 1 ? "s" : ""));

    rebuild();
}

void InlineDiffOverlay::clear()
{
    m_hunks.clear();
    rebuild();
}

void InlineDiffOverlay::rebuild()
{
    // Clear existing diff widgets
    QLayoutItem *child;
    while (m_diffLayout->count() > 1) {
        child = m_diffLayout->takeAt(0);
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    auto &tm = ThemeManager::instance();
    auto &p = tm.palette();

    for (int i = 0; i < m_hunks.size(); ++i) {
        const auto &hunk = m_hunks[i];
        if (hunk.accepted || hunk.rejected) continue;

        auto *hunkWidget = new QFrame(m_diffContent);
        hunkWidget->setStyleSheet(QStringLiteral(
            "QFrame { background: %1; border: 1px solid %2; border-radius: 4px; }")
            .arg(p.bg_base.name(), p.border_subtle.name()));

        auto *hunkLayout = new QVBoxLayout(hunkWidget);
        hunkLayout->setContentsMargins(0, 0, 0, 0);
        hunkLayout->setSpacing(0);

        // Hunk header with line number and accept/reject
        auto *hunkHeader = new QWidget(hunkWidget);
        auto *hdrLayout = new QHBoxLayout(hunkHeader);
        hdrLayout->setContentsMargins(6, 2, 6, 2);
        hdrLayout->setSpacing(4);

        auto *lineLabel = new QLabel(
            QStringLiteral("Line %1").arg(hunk.startLine), hunkHeader);
        lineLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 11px; font-family: monospace; }")
            .arg(p.text_muted.name()));
        hdrLayout->addWidget(lineLabel);
        hdrLayout->addStretch();

        auto *acceptBtn = new QPushButton("\xe2\x9c\x93", hunkHeader);
        acceptBtn->setFixedSize(20, 20);
        acceptBtn->setToolTip("Accept this change");
        acceptBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; color: %1; border: none; font-size: 13px; }"
            "QPushButton:hover { color: %2; background: %3; border-radius: 10px; }")
            .arg(p.text_muted.name(), p.green.name(), p.bg_raised.name()));
        connect(acceptBtn, &QPushButton::clicked, this, [this, i] { emit acceptHunk(i); });
        hdrLayout->addWidget(acceptBtn);

        auto *rejectBtn = new QPushButton("\xe2\x9c\x97", hunkHeader);
        rejectBtn->setFixedSize(20, 20);
        rejectBtn->setToolTip("Reject this change");
        rejectBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; color: %1; border: none; font-size: 13px; }"
            "QPushButton:hover { color: %2; background: %3; border-radius: 10px; }")
            .arg(p.text_muted.name(), p.red.name(), p.bg_raised.name()));
        connect(rejectBtn, &QPushButton::clicked, this, [this, i] { emit rejectHunk(i); });
        hdrLayout->addWidget(rejectBtn);

        hunkLayout->addWidget(hunkHeader);

        // Diff content
        auto *diffBrowser = new QTextBrowser(hunkWidget);
        diffBrowser->setFrameShape(QFrame::NoFrame);
        diffBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        diffBrowser->setStyleSheet(QStringLiteral(
            "QTextBrowser { background: transparent; border: none; "
            "font-family: 'SF Mono','JetBrains Mono','Menlo','Consolas',monospace; "
            "font-size: 12px; }"));

        QString diffHtml;
        if (!hunk.oldText.isEmpty()) {
            for (const QString &line : hunk.oldText.split('\n')) {
                diffHtml += QStringLiteral(
                    "<div style='background:%2;color:%3;padding:0 6px;white-space:pre;'>-%1</div>")
                    .arg(line.toHtmlEscaped(), tm.hex("diff_del_bg"), tm.hex("red"));
            }
        }
        if (!hunk.newText.isEmpty()) {
            for (const QString &line : hunk.newText.split('\n')) {
                diffHtml += QStringLiteral(
                    "<div style='background:%2;color:%3;padding:0 6px;white-space:pre;'>+%1</div>")
                    .arg(line.toHtmlEscaped(), tm.hex("diff_add_bg"), tm.hex("green"));
            }
        }

        diffBrowser->setHtml(diffHtml);
        int lineCount = hunk.oldLineCount + hunk.newLineCount;
        diffBrowser->setMaximumHeight(qMin(lineCount * 18 + 4, 200));
        hunkLayout->addWidget(diffBrowser);

        m_diffLayout->insertWidget(m_diffLayout->count() - 1, hunkWidget);
    }
}
