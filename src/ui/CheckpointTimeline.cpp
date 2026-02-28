#include "ui/CheckpointTimeline.h"
#include "ui/ThemeManager.h"
#include "core/Database.h"
#include "core/SnapshotManager.h"
#include <QDateTime>
#include <QFrame>
#include <QPainter>

CheckpointTimeline::CheckpointTimeline(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(4);

    // Header
    auto *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(4);

    m_titleLabel = new QLabel("Checkpoints", this);
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    m_refreshBtn = new QPushButton("\xe2\x9f\xb3", this); // refresh arrow
    m_refreshBtn->setFixedSize(22, 22);
    m_refreshBtn->setToolTip("Refresh checkpoints");
    connect(m_refreshBtn, &QPushButton::clicked, this, &CheckpointTimeline::refresh);
    headerLayout->addWidget(m_refreshBtn);

    m_layout->addLayout(headerLayout);

    // Scrollable entries
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_entriesWidget = new QWidget;
    m_entriesLayout = new QVBoxLayout(m_entriesWidget);
    m_entriesLayout->setContentsMargins(0, 0, 0, 0);
    m_entriesLayout->setSpacing(0);
    m_entriesLayout->addStretch();

    m_scrollArea->setWidget(m_entriesWidget);
    m_layout->addWidget(m_scrollArea, 1);

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &CheckpointTimeline::applyThemeColors);
}

void CheckpointTimeline::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();

    m_titleLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: bold; }")
        .arg(p.text_primary.name()));

    m_refreshBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; font-size: 14px; }"
        "QPushButton:hover { color: %2; }")
        .arg(p.text_muted.name(), p.text_primary.name()));

    rebuild();
}

void CheckpointTimeline::setDatabase(Database *db) { m_database = db; }
void CheckpointTimeline::setSnapshotManager(SnapshotManager *mgr) { m_snapshotMgr = mgr; }
void CheckpointTimeline::setSessionId(const QString &id)
{
    m_sessionId = id;
    refresh();
}

void CheckpointTimeline::refresh()
{
    if (!m_database || m_sessionId.isEmpty()) return;

    m_entries.clear();

    auto messages = m_database->loadMessages(m_sessionId);

    // Group by turnId to find checkpoints
    QMap<int, CheckpointEntry> turnMap;
    for (const auto &msg : messages) {
        if (msg.turnId <= 0) continue;
        if (!turnMap.contains(msg.turnId)) {
            CheckpointEntry entry;
            entry.turnId = msg.turnId;
            entry.sessionId = m_sessionId;
            entry.timestamp = msg.timestamp;
            turnMap[msg.turnId] = entry;
        }
        auto &entry = turnMap[msg.turnId];
        if (msg.timestamp > entry.timestamp)
            entry.timestamp = msg.timestamp;

        if (msg.role == "user")
            entry.summary = msg.content.left(60);
        if (msg.role == "tool" && msg.content.contains(":")) {
            QString file = msg.content.section(':', 1).trimmed();
            if (!file.isEmpty() && !entry.filesChanged.contains(file))
                entry.filesChanged.append(file);
        }
    }

    for (auto it = turnMap.constBegin(); it != turnMap.constEnd(); ++it)
        m_entries.append(it.value());

    rebuild();
}

void CheckpointTimeline::rebuild()
{
    // Clear existing entries
    QLayoutItem *child;
    while (m_entriesLayout->count() > 1) {
        child = m_entriesLayout->takeAt(0);
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    auto &tm = ThemeManager::instance();
    auto &p = tm.palette();

    if (m_entries.isEmpty()) {
        auto *emptyLabel = new QLabel("No checkpoints yet", m_entriesWidget);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 12px; padding: 20px; }")
            .arg(p.text_muted.name()));
        m_entriesLayout->insertWidget(0, emptyLabel);
        return;
    }

    for (int i = m_entries.size() - 1; i >= 0; --i) {
        const auto &entry = m_entries[i];

        auto *entryWidget = new QFrame(m_entriesWidget);
        entryWidget->setCursor(Qt::PointingHandCursor);
        entryWidget->setStyleSheet(QStringLiteral(
            "QFrame { background: transparent; border: none; padding: 2px 0; }"
            "QFrame:hover { background: %1; border-radius: 4px; }")
            .arg(p.hover_raised.name()));

        auto *entryLayout = new QHBoxLayout(entryWidget);
        entryLayout->setContentsMargins(4, 4, 4, 4);
        entryLayout->setSpacing(8);

        // Timeline dot
        auto *dot = new QLabel(entryWidget);
        dot->setFixedSize(10, 10);
        dot->setStyleSheet(QStringLiteral(
            "QLabel { background: %1; border-radius: 5px; }")
            .arg(entry.filesChanged.isEmpty() ? p.text_muted.name() : p.blue.name()));
        entryLayout->addWidget(dot, 0, Qt::AlignTop | Qt::AlignHCenter);

        // Content
        auto *contentLayout = new QVBoxLayout;
        contentLayout->setSpacing(1);

        QDateTime dt = QDateTime::fromSecsSinceEpoch(entry.timestamp);
        auto *timeLabel = new QLabel(
            QStringLiteral("Turn %1 \xc2\xb7 %2").arg(entry.turnId).arg(dt.toString("hh:mm")),
            entryWidget);
        timeLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 11px; font-weight: bold; }")
            .arg(p.text_secondary.name()));
        contentLayout->addWidget(timeLabel);

        if (!entry.summary.isEmpty()) {
            auto *summaryLabel = new QLabel(entry.summary, entryWidget);
            summaryLabel->setWordWrap(true);
            summaryLabel->setStyleSheet(QStringLiteral(
                "QLabel { color: %1; font-size: 11px; }")
                .arg(p.text_muted.name()));
            contentLayout->addWidget(summaryLabel);
        }

        if (!entry.filesChanged.isEmpty()) {
            QString fileText = entry.filesChanged.join(", ");
            if (fileText.length() > 50)
                fileText = fileText.left(50) + "...";
            auto *filesLabel = new QLabel(fileText, entryWidget);
            filesLabel->setStyleSheet(QStringLiteral(
                "QLabel { color: %1; font-size: 10px; font-family: monospace; }")
                .arg(p.text_faint.name()));
            contentLayout->addWidget(filesLabel);
        }

        entryLayout->addLayout(contentLayout, 1);

        // Restore button
        auto *restoreBtn = new QPushButton("\xe2\x86\xa9", entryWidget);
        restoreBtn->setFixedSize(22, 22);
        restoreBtn->setToolTip("Restore to this checkpoint");
        restoreBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; color: %1; border: none; font-size: 12px; }"
            "QPushButton:hover { color: %2; background: %3; border-radius: 11px; }")
            .arg(p.text_muted.name(), p.red.name(), p.bg_raised.name()));
        int turnId = entry.turnId;
        connect(restoreBtn, &QPushButton::clicked, this, [this, turnId] {
            emit restoreRequested(turnId);
        });
        entryLayout->addWidget(restoreBtn, 0, Qt::AlignTop);

        m_entriesLayout->insertWidget(m_entriesLayout->count() - 1, entryWidget);
    }
}
