#include "ui/EffectsPanel.h"
#include "ui/ThemeManager.h"
#include "ui/FileIconProvider.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFileInfo>
#include <QScrollBar>
#include <QDateTime>
#include <algorithm>
#include <cmath>

// ─── EffectsPanelEmptyState ─────────────────────────────────────────────────

class EffectsPanelEmptyState : public QWidget {
public:
    explicit EffectsPanelEmptyState(QWidget *parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        const auto &pal = ThemeManager::instance().palette();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), pal.surface0);

        QPoint c = rect().center();
        int cx = c.x();
        int iconCy = c.y() - 24;

        // Document body
        p.setPen(Qt::NoPen);
        p.setBrush(pal.bg_raised);
        p.drawRoundedRect(cx - 14, iconCy - 18, 28, 36, 3, 3);

        // Folded corner cutout
        p.setBrush(pal.surface0);
        QPolygon cutout;
        cutout << QPoint(cx + 4, iconCy - 18)
               << QPoint(cx + 14, iconCy - 8)
               << QPoint(cx + 14, iconCy - 18);
        p.drawPolygon(cutout);

        // Fold fill
        p.setBrush(pal.hover_raised);
        QPolygon fold;
        fold << QPoint(cx + 4, iconCy - 18)
             << QPoint(cx + 14, iconCy - 8)
             << QPoint(cx + 4, iconCy - 8);
        p.drawPolygon(fold);

        // Text line hints inside document
        p.setBrush(pal.pressed_raised);
        for (int i = 0; i < 3; ++i) {
            int lw = (i == 2) ? 10 : 18;
            p.drawRoundedRect(cx - 10, iconCy - 10 + i * 7, lw, 2, 1, 1);
        }

        // Checkmark circle background
        QColor greenBg = pal.green;
        greenBg.setAlpha(50);
        p.setBrush(greenBg);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(cx + 10, iconCy + 14), 7, 7);

        // Checkmark stroke
        p.setPen(QPen(pal.green, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPolygonF check;
        check << QPointF(cx + 6, iconCy + 14)
              << QPointF(cx + 9, iconCy + 17)
              << QPointF(cx + 14, iconCy + 11);
        p.drawPolyline(check);

        // Primary label
        QFont tf = font();
        tf.setPixelSize(12);
        tf.setWeight(QFont::Medium);
        p.setFont(tf);
        p.setPen(pal.text_muted);
        p.drawText(QRect(cx - 100, c.y() + 24, 200, 18),
                   Qt::AlignCenter, "No changes yet");

        // Secondary label
        QFont hf = font();
        hf.setPixelSize(11);
        p.setFont(hf);
        p.setPen(pal.surface2);
        p.drawText(QRect(cx - 160, c.y() + 42, 320, 18),
                   Qt::AlignCenter,
                   "File edits will appear as your agent works");
    }
};

// ─── FileChangeItem ─────────────────────────────────────────────────────────

FileChangeItem::FileChangeItem(const FileChange &change, const QString &rootPath,
                               QWidget *parent)
    : QWidget(parent)
{
    m_filePath = change.filePath;
    m_type = change.type;
    m_linesAdded = change.linesAdded;
    m_linesRemoved = change.linesRemoved;

    // Compute relative path
    if (!rootPath.isEmpty() && m_filePath.startsWith(rootPath))
        m_relativePath = m_filePath.mid(rootPath.length() + 1);
    else
        m_relativePath = m_filePath;

    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(40);
    setMouseTracking(true);
}

void FileChangeItem::update(const FileChange &change)
{
    m_type = change.type;
    m_linesAdded = change.linesAdded;
    m_linesRemoved = change.linesRemoved;
    QWidget::update();
}

QSize FileChangeItem::sizeHint() const
{
    return {200, 40};
}

void FileChangeItem::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto &pal = ThemeManager::instance().palette();

    // Hover background
    if (m_hovered) {
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(2, 1, -2, -1), 4, 4);
        p.fillPath(path, QColor(pal.text_primary.red(), pal.text_primary.green(),
                                 pal.text_primary.blue(), 8));
    }

    int x = 10;

    int midY = height() / 2;

    // File icon
    QIcon icon = FileIconProvider::iconForFile(m_filePath);
    if (!icon.isNull()) {
        icon.paint(&p, x, midY - 9, 18, 18);
        x += 24;
    }

    // File name + directory
    QFileInfo fi(m_relativePath);
    QString fileName = fi.fileName();
    QString dirPath = fi.path();
    if (dirPath == ".") dirPath.clear();

    QFont nameFont = font();
    nameFont.setPixelSize(11);
    nameFont.setWeight(QFont::Medium);
    p.setFont(nameFont);
    p.setPen(pal.text_primary);

    int textBaseY = midY + p.fontMetrics().ascent() / 2;
    int nameW = p.fontMetrics().horizontalAdvance(fileName);
    p.drawText(x, textBaseY, fileName);
    x += nameW;

    if (!dirPath.isEmpty()) {
        QFont dirFont = font();
        dirFont.setPixelSize(10);
        p.setFont(dirFont);
        p.setPen(pal.text_muted);
        int availW = width() - x - 80;
        if (availW > 30) {
            QString dirStr = " " + dirPath;
            int fullW = p.fontMetrics().horizontalAdvance(dirStr);
            if (fullW <= availW) {
                p.drawText(x, textBaseY, dirStr);
            } else {
                p.drawText(x, textBaseY,
                    p.fontMetrics().elidedText(dirStr, Qt::ElideLeft, availW));
            }
        }
    }

    // Badge (M/A/D)
    int badgeX = width() - 72;
    QFont badgeFont = font();
    badgeFont.setPixelSize(9);
    badgeFont.setWeight(QFont::Bold);
    p.setFont(badgeFont);

    QString badge;
    QColor badgeColor;
    switch (m_type) {
    case FileChange::Created:
        badge = "A";
        badgeColor = pal.green;
        break;
    case FileChange::Deleted:
        badge = "D";
        badgeColor = pal.red;
        break;
    default:
        badge = "M";
        badgeColor = pal.yellow;
        break;
    }

    // Badge background — centered vertically
    QRect badgeRect(badgeX, midY - 9, 18, 18);
    QPainterPath badgePath;
    badgePath.addRoundedRect(badgeRect, 4, 4);
    QColor badgeBg = badgeColor;
    badgeBg.setAlphaF(0.15);
    p.fillPath(badgePath, badgeBg);
    p.setPen(badgeColor);
    p.drawText(badgeRect, Qt::AlignCenter, badge);

    // Line delta — centered vertically
    int deltaX = badgeX + 24;
    QFont deltaFont = font();
    deltaFont.setPixelSize(10);
    p.setFont(deltaFont);
    int deltaBaseY = midY + p.fontMetrics().ascent() / 2;

    if (m_linesAdded > 0) {
        p.setPen(pal.green);
        QString addStr = QStringLiteral("+%1").arg(m_linesAdded);
        p.drawText(deltaX, deltaBaseY, addStr);
        deltaX += p.fontMetrics().horizontalAdvance(addStr) + 3;
    }
    if (m_linesRemoved > 0) {
        p.setPen(pal.red);
        p.drawText(deltaX, deltaBaseY, QStringLiteral("-%1").arg(m_linesRemoved));
    }
}

void FileChangeItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked(m_filePath);
    QWidget::mousePressEvent(event);
}

void FileChangeItem::enterEvent(QEnterEvent *)
{
    m_hovered = true;
    QWidget::update();
}

void FileChangeItem::leaveEvent(QEvent *)
{
    m_hovered = false;
    QWidget::update();
}

// ─── EffectsPanel ───────────────────────────────────────────────────────────

EffectsPanel::EffectsPanel(QWidget *parent)
    : QWidget(parent)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Header
    m_header = new QWidget(this);
    m_header->setFixedHeight(44);
    auto *headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(12, 8, 8, 0);

    m_headerLabel = new QLabel("Effects", m_header);
    QFont hf = m_headerLabel->font();
    hf.setPixelSize(10);
    hf.setWeight(QFont::DemiBold);
    hf.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    m_headerLabel->setFont(hf);

    m_scopeToggle = new QPushButton("Session", m_header);
    m_scopeToggle->setFixedHeight(22);
    m_scopeToggle->setCursor(Qt::PointingHandCursor);
    connect(m_scopeToggle, &QPushButton::clicked, this, [this] {
        m_showAllSessions = !m_showAllSessions;
        m_scopeToggle->setText(m_showAllSessions ? "All" : "Session");
        rebuildList();
    });

    headerLayout->addWidget(m_headerLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_scopeToggle);
    m_mainLayout->addWidget(m_header);

    // Stats bar
    m_statsLabel = new QLabel(this);
    m_statsLabel->setFixedHeight(26);
    m_statsLabel->setContentsMargins(12, 2, 12, 2);
    m_mainLayout->addWidget(m_statsLabel);

    // File list scroll area
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_scrollContent = new QWidget(m_scrollArea);
    m_scrollContent->setObjectName("effectsScrollContent");
    m_fileLayout = new QVBoxLayout(m_scrollContent);
    m_fileLayout->setContentsMargins(4, 4, 4, 4);
    m_fileLayout->setSpacing(1);
    m_fileLayout->addStretch();

    m_scrollArea->setWidget(m_scrollContent);
    m_mainLayout->addWidget(m_scrollArea, 1);

    m_emptyState = new EffectsPanelEmptyState(this);
    m_mainLayout->addWidget(m_emptyState, 1);
    // Start with empty state visible, scroll area hidden
    m_scrollArea->hide();
    m_statsLabel->hide();
    m_emptyState->show();

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &EffectsPanel::applyThemeColors);
}

bool EffectsPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto *label = qobject_cast<QLabel *>(obj);
        if (label) {
            bool ok = false;
            int turnId = label->property("turnId").toInt(&ok);
            if (ok && turnId > 0)
                emit turnClicked(turnId);
        }
    }
    return QWidget::eventFilter(obj, event);
}

void EffectsPanel::setRootPath(const QString &rootPath)
{
    m_rootPath = rootPath;
}

void EffectsPanel::setCurrentSession(const QString &sessionId)
{
    m_currentSessionId = sessionId;
    if (!m_showAllSessions)
        rebuildList();
}

void EffectsPanel::setCurrentTurnId(int turnId)
{
    m_currentTurnId = turnId;
}

void EffectsPanel::setHighlightedTurn(int turnId)
{
    if (m_highlightedTurn == turnId) return;
    m_highlightedTurn = turnId;
    updateTurnDividerLabels();

    // Scroll the effects panel to show the highlighted turn
    if (m_turnDividers.contains(turnId)) {
        auto *divider = m_turnDividers[turnId];
        m_scrollArea->ensureWidgetVisible(divider, 0, 40);
    }
}

void EffectsPanel::setTurnTimestamps(const QMap<int, qint64> &timestamps)
{
    m_turnTimestamps = timestamps;
    updateTurnDividerLabels();
}

void EffectsPanel::onFileChanged(const QString &filePath, const FileChange &change)
{
    QString sid = change.sessionId.isEmpty() ? m_currentSessionId : change.sessionId;
    int turn = change.turnId > 0 ? change.turnId : m_currentTurnId;

    m_sessionChanges[sid][turn][filePath] = change;

    bool visible = m_showAllSessions || sid == m_currentSessionId;
    if (!visible) return;

    rebuildList();
}

void EffectsPanel::populateFromHistory(const QString &sessionId, const QList<FileChange> &changes)
{
    for (const auto &change : changes) {
        int turn = change.turnId > 0 ? change.turnId : 0;
        m_sessionChanges[sessionId][turn][change.filePath] = change;
    }

    if (m_currentSessionId == sessionId || m_showAllSessions)
        rebuildList();
}

bool EffectsPanel::hasChangesForSession(const QString &sessionId) const
{
    if (!m_sessionChanges.contains(sessionId)) return false;
    for (auto turnIt = m_sessionChanges[sessionId].constBegin();
         turnIt != m_sessionChanges[sessionId].constEnd(); ++turnIt) {
        if (!turnIt.value().isEmpty()) return true;
    }
    return false;
}

void EffectsPanel::clear()
{
    for (auto *item : m_items)
        item->deleteLater();
    m_items.clear();
    for (auto *div : m_turnDividers)
        div->deleteLater();
    m_turnDividers.clear();
    m_sessionChanges.clear();
    m_turnTimestamps.clear();
    m_highlightedTurn = -1;
    updateStats();
}

void EffectsPanel::rebuildList()
{
    // Remove existing items and dividers
    for (auto *item : m_items)
        item->deleteLater();
    m_items.clear();
    for (auto *div : m_turnDividers)
        div->deleteLater();
    m_turnDividers.clear();

    // Collect visible (turnId, filePath, FileChange) entries
    struct TurnEntry {
        int turnId;
        QString filePath;
        FileChange change;
    };
    QList<TurnEntry> entries;

    auto collectSession = [&](const QString &sid) {
        if (!m_sessionChanges.contains(sid)) return;
        auto &turns = m_sessionChanges[sid];
        for (auto turnIt = turns.constBegin(); turnIt != turns.constEnd(); ++turnIt) {
            for (auto fileIt = turnIt.value().constBegin();
                 fileIt != turnIt.value().constEnd(); ++fileIt) {
                entries.append({turnIt.key(), fileIt.key(), fileIt.value()});
            }
        }
    };

    if (m_showAllSessions) {
        for (auto sit = m_sessionChanges.constBegin();
             sit != m_sessionChanges.constEnd(); ++sit)
            collectSession(sit.key());
    } else {
        collectSession(m_currentSessionId);
    }

    // Sort by turnId ascending (oldest first), then filePath ascending
    std::sort(entries.begin(), entries.end(),
              [](const TurnEntry &a, const TurnEntry &b) {
        if (a.turnId != b.turnId) return a.turnId < b.turnId;
        return a.filePath < b.filePath;
    });

    // Render with turn dividers
    int lastTurnId = -1;

    for (const auto &entry : entries) {
        if (entry.turnId != lastTurnId && entry.turnId > 0) {
            auto *divider = new QLabel(m_scrollContent);
            divider->setFixedHeight(30);
            divider->setContentsMargins(10, 10, 8, 4);
            divider->setCursor(Qt::PointingHandCursor);
            QFont df = divider->font();
            df.setPixelSize(10);
            df.setWeight(QFont::DemiBold);
            df.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
            divider->setFont(df);
            divider->setTextFormat(Qt::RichText);
            m_fileLayout->insertWidget(m_fileLayout->count() - 1, divider);
            m_turnDividers[entry.turnId] = divider;

            // Make divider clickable
            int clickTurnId = entry.turnId;
            divider->installEventFilter(this);
            divider->setProperty("turnId", clickTurnId);

            lastTurnId = entry.turnId;
        }

        auto *item = new FileChangeItem(entry.change, m_rootPath, m_scrollContent);
        connect(item, &FileChangeItem::clicked, this, &EffectsPanel::fileClicked);
        m_fileLayout->insertWidget(m_fileLayout->count() - 1, item);
        QString itemKey = QStringLiteral("%1:%2").arg(entry.turnId).arg(entry.filePath);
        m_items[itemKey] = item;
    }

    updateTurnDividerLabels();
    updateStats();
}

void EffectsPanel::updateStats()
{
    int totalAdded = 0, totalRemoved = 0;
    QSet<QString> uniqueFiles;

    auto collectSession = [&](const QString &sid) {
        if (!m_sessionChanges.contains(sid)) return;
        auto &turns = m_sessionChanges[sid];
        for (auto turnIt = turns.constBegin(); turnIt != turns.constEnd(); ++turnIt) {
            for (auto fileIt = turnIt.value().constBegin();
                 fileIt != turnIt.value().constEnd(); ++fileIt) {
                uniqueFiles.insert(fileIt.key());
                totalAdded += fileIt->linesAdded;
                totalRemoved += fileIt->linesRemoved;
            }
        }
    };

    if (m_showAllSessions) {
        for (auto sit = m_sessionChanges.constBegin();
             sit != m_sessionChanges.constEnd(); ++sit)
            collectSession(sit.key());
    } else {
        collectSession(m_currentSessionId);
    }

    int totalFiles = uniqueFiles.size();

    auto &pal = ThemeManager::instance().palette();
    if (totalFiles == 0) {
        m_statsLabel->hide();
        m_scrollArea->hide();
        m_emptyState->show();
    } else {
        m_emptyState->hide();
        m_scrollArea->show();
        m_statsLabel->show();
        m_statsLabel->setText(QStringLiteral(
            "<span style='color:%1;font-size:11px;font-weight:500;'>%2 file%3</span>"
            "<span style='color:%4;font-size:11px;'> \u00b7 </span>"
            "<span style='color:%5;font-size:11px;font-weight:600;'>+%6</span>"
            "<span style='color:%4;font-size:11px;'> </span>"
            "<span style='color:%7;font-size:11px;font-weight:600;'>-%8</span>")
            .arg(pal.text_secondary.name())
            .arg(totalFiles)
            .arg(totalFiles > 1 ? "s" : "")
            .arg(pal.text_muted.name())
            .arg(pal.green.name())
            .arg(totalAdded)
            .arg(pal.red.name())
            .arg(totalRemoved));
        m_statsLabel->setTextFormat(Qt::RichText);
    }
}

void EffectsPanel::updateTurnDividerLabels()
{
    auto &pal = ThemeManager::instance().palette();
    qint64 refTimestamp = 0;
    if (m_highlightedTurn > 0 && m_turnTimestamps.contains(m_highlightedTurn))
        refTimestamp = m_turnTimestamps[m_highlightedTurn];

    for (auto it = m_turnDividers.constBegin(); it != m_turnDividers.constEnd(); ++it) {
        int turnId = it.key();
        QLabel *div = it.value();
        bool highlighted = (turnId == m_highlightedTurn);

        // Build label text: "TURN N" + optional relative time
        QString text = QStringLiteral("TURN %1").arg(turnId);
        QString timeStr;
        if (refTimestamp > 0 && m_turnTimestamps.contains(turnId)) {
            qint64 turnTs = m_turnTimestamps[turnId];
            if (turnTs != refTimestamp)
                timeStr = formatRelativeTime(refTimestamp, turnTs);
        }

        // Build rich text
        QColor labelColor = highlighted ? pal.teal : pal.text_muted;
        QColor timeColor = highlighted ? pal.teal : pal.text_muted;
        timeColor.setAlphaF(highlighted ? 0.7f : 0.5f);

        QString html = QStringLiteral(
            "<span style='color:%1;font-size:10px;'>%2</span>")
            .arg(labelColor.name(), text);
        if (!timeStr.isEmpty()) {
            html += QStringLiteral(
                "<span style='color:%1;font-size:10px;'> \u00b7 %2</span>")
                .arg(timeColor.name(), timeStr);
        }
        div->setText(html);

        // Highlight background for current turn
        if (highlighted) {
            div->setStyleSheet(QStringLiteral(
                "QLabel { background: %1; border-left: 2px solid %2; }")
                .arg(QColor(pal.teal.red(), pal.teal.green(), pal.teal.blue(), 15).name(QColor::HexArgb),
                     pal.teal.name()));
        } else {
            div->setStyleSheet(QStringLiteral(
                "QLabel { background: transparent; border-left: none; }"));
        }
    }
}

QString EffectsPanel::formatRelativeTime(qint64 refTimestamp, qint64 turnTimestamp) const
{
    qint64 diff = turnTimestamp - refTimestamp;  // positive = future, negative = past
    qint64 absDiff = std::abs(diff);

    QString timeStr;
    if (absDiff < 60)
        timeStr = QStringLiteral("%1s").arg(absDiff);
    else if (absDiff < 3600)
        timeStr = QStringLiteral("%1 min").arg(absDiff / 60);
    else if (absDiff < 86400)
        timeStr = QStringLiteral("%1h %2m").arg(absDiff / 3600).arg((absDiff % 3600) / 60);
    else
        timeStr = QStringLiteral("%1d").arg(absDiff / 86400);

    return (diff >= 0 ? "+" : "\u2212") + timeStr;
}

void EffectsPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), ThemeManager::instance().palette().surface0);
}

void EffectsPanel::applyThemeColors()
{
    auto &thm = ThemeManager::instance();

    m_header->setStyleSheet(QStringLiteral("background: %1;").arg(thm.hex("bg_raised")));

    m_headerLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background: transparent; }").arg(thm.hex("text_muted")));

    m_scopeToggle->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 4px; font-size: 10px; padding: 2px 8px; }"
        "QPushButton:hover { background: %4; color: %5; }")
        .arg(thm.hex("bg_raised"), thm.hex("text_muted"), thm.hex("border_subtle"),
             thm.hex("bg_raised"), thm.hex("text_primary")));

    m_scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QWidget#effectsScrollContent { background: transparent; }"));
    m_scrollArea->viewport()->setAutoFillBackground(false);

    updateStats();
}
