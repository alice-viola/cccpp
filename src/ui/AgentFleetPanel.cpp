#include "ui/AgentFleetPanel.h"
#include "ui/ThemeManager.h"
#include "core/PersonalityProfile.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDate>
#include <algorithm>
#include <cmath>

// ─── AgentCard ──────────────────────────────────────────────────────────────

AgentCard::AgentCard(const QString &sessionId, QWidget *parent)
    : QWidget(parent), m_sessionId(sessionId)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setFixedHeight(sizeHint().height());

    // Cache fonts to avoid recreating them every paintEvent
    m_titleFont = font();
    m_titleFont.setPixelSize(12);
    m_titleFont.setWeight(QFont::Medium);
    m_smallFont = font();
    m_smallFont.setPixelSize(10);
}

void AgentCard::update(const AgentSummary &summary)
{
    m_title = summary.title.isEmpty() ? "New Agent" : summary.title;
    m_activity = summary.activity;
    m_processing = summary.processing;
    m_blocked = summary.hasPendingQuestion;
    m_unread = summary.unread;
    m_favorite = summary.favorite;
    m_editCount = summary.editCount;
    m_turnCount = summary.turnCount;
    m_depth = summary.depth;
    m_isOrchestratorRoot = summary.isOrchestratorRoot;
    m_childCount = summary.childCount;
    m_costUsd = summary.costUsd;
    m_updatedAt = summary.updatedAt;
    m_profileIds = summary.profileIds;
    updatePulseAnimation();
    setFixedHeight(sizeHint().height());
    QWidget::update();
}

void AgentCard::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    QWidget::update();
}

void AgentCard::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    setFixedHeight(m_collapsed ? 36 : sizeHint().height());
    updateGeometry();
    QWidget::update();
}

void AgentCard::setChildrenCollapsed(bool collapsed)
{
    if (m_childrenCollapsed == collapsed) return;
    m_childrenCollapsed = collapsed;
    QWidget::update();
}

void AgentCard::setPulsePhase(float phase)
{
    m_pulsePhase = phase;
    QWidget::update();
}

void AgentCard::updatePulseAnimation()
{
    if (m_processing && !m_pulseAnim) {
        m_pulseAnim = new QPropertyAnimation(this, "pulsePhase", this);
        m_pulseAnim->setDuration(1600);
        m_pulseAnim->setStartValue(0.3f);
        m_pulseAnim->setEndValue(1.0f);
        m_pulseAnim->setLoopCount(-1);
        m_pulseAnim->setEasingCurve(QEasingCurve::InOutSine);
        m_pulseAnim->start();
    } else if (!m_processing && m_pulseAnim) {
        m_pulseAnim->stop();
        m_pulseAnim->deleteLater();
        m_pulseAnim = nullptr;
        m_pulsePhase = 1.0f;
    }
}

QSize AgentCard::sizeHint() const
{
    if (m_collapsed) return {36, 36};
    return {200, 56};
}

QSize AgentCard::minimumSizeHint() const
{
    return m_collapsed ? QSize(36, 36) : QSize(100, 52);
}

void AgentCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto &pal = ThemeManager::instance().palette();

    // ─── Intensity computation ───
    float turnIntensity = qMin(1.0f, m_turnCount / 8.0f);
    float recencyBoost = 0.0f;
    if (m_updatedAt > 0) {
        qint64 ageSecs = QDateTime::currentSecsSinceEpoch() - m_updatedAt;
        if (ageSecs < 3600)
            recencyBoost = 0.3f * (1.0f - ageSecs / 3600.0f);
        else if (ageSecs < 86400)
            recencyBoost = 0.1f * (1.0f - ageSecs / 86400.0f);
    }
    float intensity = qMin(1.0f, turnIntensity + recencyBoost);

    QColor tintColor = m_processing ? pal.green : pal.teal;

    // Intensity wash (subtle background tint)
    if (intensity > 0.01f && !m_collapsed && !m_selected) {
        QPainterPath washPath;
        washPath.addRoundedRect(rect().adjusted(2, 1, -2, -1), 6, 6);
        QColor wash = tintColor;
        wash.setAlpha(static_cast<int>(intensity * 61));
        p.fillPath(washPath, wash);
    }

    // Selection/hover background on top
    QColor bg = Qt::transparent;
    if (m_selected)
        bg = pal.surface1;
    else if (m_hovered)
        bg = QColor(pal.text_primary.red(), pal.text_primary.green(),
                     pal.text_primary.blue(), 8);

    if (bg != Qt::transparent) {
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(2, 1, -2, -1), 6, 6);
        p.fillPath(path, bg);
    }

    // Selected border
    if (m_selected) {
        QPainterPath borderPath;
        borderPath.addRoundedRect(QRectF(rect()).adjusted(2.5, 1.5, -2.5, -1.5), 6, 6);
        p.setPen(QPen(pal.teal, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(borderPath);
    }

    // Status dot color
    QColor dotColor;
    if (m_blocked)
        dotColor = pal.yellow;
    else if (m_processing)
        dotColor = pal.green;
    else
        dotColor = pal.surface2;

    if (m_processing)
        dotColor.setAlphaF(0.4 + 0.6 * m_pulsePhase);

    if (m_collapsed) {
        // Collapsed mode: centered dot, size varies with intensity
        float dotR = 5.0f + intensity * 2.0f;
        QPointF center(width() / 2.0, height() / 2.0);
        p.setPen(Qt::NoPen);
        p.setBrush(dotColor);
        p.drawEllipse(center, dotR, dotR);

        if (m_isOrchestratorRoot) {
            p.setPen(QPen(pal.mauve, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(center, dotR + 3, dotR + 3);
        } else if (m_unread) {
            p.setPen(QPen(pal.teal, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(center, dotR + 3, dotR + 3);
        }
        return;
    }

    // ─── Expanded mode ───
    int depthIndent = m_depth * 16;
    int leftPad = 14 + depthIndent;
    int textRight = width() - 10;

    // Draw connector lines for child agents
    if (m_depth > 0) {
        p.setPen(QPen(pal.surface2, 1.0));
        int lineX = 14 + (m_depth - 1) * 16 + 4;
        p.drawLine(lineX, 0, lineX, height());
        p.drawLine(lineX, height() / 2, lineX + 10, height() / 2);
    }

    // Orchestrator root: mauve accent bar
    m_chevronRect = QRect();
    if (m_isOrchestratorRoot) {
        QPainterPath accentPath;
        accentPath.addRoundedRect(QRectF(2, 4, 3, height() - 8), 1.5, 1.5);
        QColor accentColor = pal.mauve;
        accentColor.setAlpha(m_selected ? 200 : 140);
        p.fillPath(accentPath, accentColor);

        // Chevron toggle
        int chX = 9;
        int chY = height() / 2;
        m_chevronRect = QRect(0, chY - 10, 18, 20);
        p.setPen(QPen(m_hovered ? pal.text_secondary : pal.text_muted, 1.3));
        p.setBrush(Qt::NoBrush);
        if (m_childrenCollapsed) {
            // Right-pointing chevron ▸
            p.drawLine(chX - 2, chY - 4, chX + 2, chY);
            p.drawLine(chX + 2, chY, chX - 2, chY + 4);
        } else {
            // Down-pointing chevron ▾
            p.drawLine(chX - 3, chY - 2, chX, chY + 2);
            p.drawLine(chX, chY + 2, chX + 3, chY - 2);
        }
    }

    // Title line
    p.setFont(m_titleFont);
    p.setPen(m_selected ? pal.text_primary : pal.text_secondary);

    // Status dot before title
    int dotX = leftPad;
    int titleY = 22;
    float dotR = (m_blocked || m_processing) ? 4.0f : 3.0f;
    p.setPen(Qt::NoPen);
    p.setBrush(dotColor);
    p.drawEllipse(QPointF(dotX, titleY - 1), dotR, dotR);

    int textX = dotX + 12;

    // Favorite star after status dot
    if (m_favorite) {
        QColor starColor(0xf9, 0xe2, 0xaf); // Catppuccin yellow
        p.setPen(Qt::NoPen);
        p.setBrush(starColor);
        // Draw a 5-pointed star centered at (textX + 5, titleY - 1), radius 5
        float cx = textX + 5, cy = titleY - 1, outerR = 5.0f, innerR = 2.2f;
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            float angle = -M_PI / 2.0f + i * M_PI / 5.0f;
            float r = (i % 2 == 0) ? outerR : innerR;
            star << QPointF(cx + r * std::cos(angle), cy + r * std::sin(angle));
        }
        p.drawPolygon(star);
        textX += 14;
    }

    // Delete button (trash) on hover — top right
    m_deleteRect = QRect();
    if (m_hovered) {
        int trashX = width() - 28;
        int trashY = 6;
        m_deleteRect = QRect(trashX, trashY, 20, 20);

        // Draw simple trash icon
        p.setPen(QPen(pal.text_faint, 1.2));
        p.setBrush(Qt::NoBrush);
        // Lid
        p.drawLine(trashX + 5, trashY + 6, trashX + 15, trashY + 6);
        p.drawLine(trashX + 8, trashY + 4, trashX + 12, trashY + 4);
        // Body
        p.drawLine(trashX + 6, trashY + 6, trashX + 7, trashY + 16);
        p.drawLine(trashX + 14, trashY + 6, trashX + 13, trashY + 16);
        p.drawLine(trashX + 7, trashY + 16, trashX + 13, trashY + 16);

        textRight = trashX - 4;
    }

    // Draw title text
    p.setFont(m_titleFont);
    p.setPen(m_selected ? pal.text_primary : pal.text_secondary);
    QString elidedTitle = p.fontMetrics().elidedText(m_title, Qt::ElideRight, textRight - textX);
    p.drawText(textX, titleY, elidedTitle);

    // Second line: collapsed badge, activity, or date
    bool showCollapsedBadge = m_isOrchestratorRoot && m_childrenCollapsed && m_childCount > 0;
    bool showActivity = m_processing && !m_activity.isEmpty();
    if (showCollapsedBadge) {
        p.setFont(m_smallFont);
        p.setPen(pal.mauve);
        QString label = QString("%1 agent%2").arg(m_childCount).arg(m_childCount > 1 ? "s" : "");
        p.drawText(textX, 42, label);
    } else if (showActivity) {
        p.setFont(m_smallFont);
        p.setPen(pal.green);
        int actW = width() - textX - 10;
        QString elidedAct = p.fontMetrics().elidedText(m_activity, Qt::ElideRight, actW);
        p.drawText(textX, 42, elidedAct);
    } else if (m_updatedAt > 0) {
        p.setFont(m_smallFont);
        p.setPen(pal.text_muted);

        QDateTime dt = QDateTime::fromSecsSinceEpoch(m_updatedAt);
        QDate today = QDate::currentDate();
        QDate date = dt.date();
        QString dateStr;
        if (date == today)
            dateStr = "Today " + dt.toString("h:mm AP");
        else if (date == today.addDays(-1))
            dateStr = "Yesterday " + dt.toString("h:mm AP");
        else
            dateStr = dt.toString("MMM d");

        p.drawText(textX, 42, dateStr);
    }

    // Unread dot
    if (m_unread && !m_selected) {
        p.setPen(Qt::NoPen);
        p.setBrush(pal.teal);
        p.drawEllipse(QPointF(width() - 10, 12), 3, 3);
    }
}

void AgentCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_hovered && m_deleteRect.isValid() && m_deleteRect.contains(event->pos())) {
            emit deleteRequested(m_sessionId);
            return;
        }
        if (m_isOrchestratorRoot && m_chevronRect.isValid() && m_chevronRect.contains(event->pos())) {
            m_childrenCollapsed = !m_childrenCollapsed;
            emit collapseToggled(m_sessionId, m_childrenCollapsed);
            QWidget::update();
            return;
        }
        emit clicked(m_sessionId);
    }
    QWidget::mousePressEvent(event);
}

void AgentCard::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit doubleClicked(m_sessionId);
}

void AgentCard::contextMenuEvent(QContextMenuEvent *event)
{
    auto &thm = ThemeManager::instance();
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: %1; border: 1px solid %2; border-radius: 6px; padding: 4px 0; }"
        "QMenu::item { color: %3; padding: 6px 16px; font-size: 12px; }"
        "QMenu::item:selected { background: %4; }")
        .arg(thm.hex("bg_raised"), thm.hex("border_standard"),
             thm.hex("text_secondary"), thm.hex("surface1")));

    menu.addAction("Rename\u2026", this, [this]() {
        emit renameRequested(m_sessionId);
    });
    menu.addAction(m_favorite ? "Unfavorite" : "Favorite", this, [this]() {
        emit favoriteToggled(m_sessionId, !m_favorite);
    });
    menu.addSeparator();
    menu.addAction("Delete", this, [this]() {
        emit deleteRequested(m_sessionId);
    });

    menu.exec(event->globalPos());
}

void AgentCard::enterEvent(QEnterEvent *)
{
    m_hovered = true;
    QWidget::update();
}

void AgentCard::leaveEvent(QEvent *)
{
    m_hovered = false;
    QWidget::update();
}

// ─── AgentFleetPanel ────────────────────────────────────────────────────────

AgentFleetPanel::AgentFleetPanel(QWidget *parent)
    : QWidget(parent)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Header
    m_header = new QWidget(this);
    m_header->setFixedHeight(44);
    auto *headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(12, 8, 4, 0);

    m_headerLabel = new QLabel("Agents", m_header);
    QFont hf = m_headerLabel->font();
    hf.setPixelSize(10);
    hf.setWeight(QFont::DemiBold);
    hf.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    m_headerLabel->setFont(hf);

    m_newAgentBtn = new QPushButton(m_header);
    m_newAgentBtn->setFixedSize(26, 26);
    m_newAgentBtn->setToolTip("New Agent (Ctrl+N)");
    m_newAgentBtn->setCursor(Qt::PointingHandCursor);
    connect(m_newAgentBtn, &QPushButton::clicked, this, &AgentFleetPanel::newAgentRequested);

    m_menuBtn = new QPushButton(m_header);
    m_menuBtn->setFixedSize(26, 26);
    m_menuBtn->setToolTip("Actions");
    m_menuBtn->setCursor(Qt::PointingHandCursor);

    m_actionsMenu = new QMenu(m_menuBtn);
    m_actionsMenu->addAction("Keep only today's chats", this, [this]() {
        emit deleteAllExceptTodayRequested();
    });
    m_actionsMenu->addAction("Delete older than 1 day", this, [this]() {
        emit deleteOlderThanDayRequested();
    });
    m_actionsMenu->addAction("Delete all", this, [this]() {
        emit deleteAllRequested();
    });
    connect(m_menuBtn, &QPushButton::clicked, this, [this]() {
        m_actionsMenu->popup(m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height())));
    });

    headerLayout->addWidget(m_headerLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_newAgentBtn);
    headerLayout->addWidget(m_menuBtn);
    m_mainLayout->addWidget(m_header);

    // Scroll area for agent cards
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_scrollContent = new QWidget(m_scrollArea);
    m_agentLayout = new QVBoxLayout(m_scrollContent);
    m_agentLayout->setContentsMargins(4, 2, 4, 4);
    m_agentLayout->setSpacing(2);
    m_agentLayout->addStretch();

    m_scrollArea->setWidget(m_scrollContent);
    m_mainLayout->addWidget(m_scrollArea, 1);

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &AgentFleetPanel::applyThemeColors);
}

void AgentFleetPanel::setCollapsed(bool)
{
    // Agent panel never collapses
}

QList<AgentSummary> AgentFleetPanel::buildHierarchicalOrder(const QList<AgentSummary> &flat) const
{
    QMap<QString, AgentSummary> byId;
    QMap<QString, QList<QString>> childrenOf;
    QList<QString> roots;

    for (const auto &a : flat) {
        byId[a.sessionId] = a;
        if (a.parentSessionId.isEmpty())
            roots.append(a.sessionId);
        else
            childrenOf[a.parentSessionId].append(a.sessionId);
    }

    // Promote orphans (children whose parent was deleted) to roots
    for (auto it = childrenOf.constBegin(); it != childrenOf.constEnd(); ++it) {
        if (!byId.contains(it.key())) {
            for (const auto &orphanId : it.value())
                roots.append(orphanId);
        }
    }

    QList<AgentSummary> result;
    std::function<void(const QString &, int)> visit = [&](const QString &id, int depth) {
        if (!byId.contains(id)) return;
        auto s = byId[id];
        s.depth = depth;
        s.isDelegatedChild = (depth > 0);
        s.isOrchestratorRoot = (depth == 0 && childrenOf.contains(id) && !childrenOf[id].isEmpty());
        s.childCount = childrenOf.value(id).size();
        result.append(s);
        auto children = childrenOf.value(id);
        // Sort children by createdAt ascending (stable — doesn't change during processing)
        std::sort(children.begin(), children.end(), [&byId](const QString &a, const QString &b) {
            return byId[a].createdAt < byId[b].createdAt;
        });
        for (const auto &childId : children)
            visit(childId, depth + 1);
    };

    for (const auto &rootId : roots)
        visit(rootId, 0);

    return result;
}

void AgentFleetPanel::rebuild(const QList<AgentSummary> &agents, const QString &selectedId)
{
    m_selectedId = selectedId;

    // Sort by updatedAt descending (most recent first), then apply hierarchy
    auto sorted = agents;
    std::sort(sorted.begin(), sorted.end(), [](const AgentSummary &a, const AgentSummary &b) {
        return a.updatedAt > b.updatedAt;
    });

    // Check if any agents have parent relationships
    bool hasHierarchy = false;
    for (const auto &a : sorted) {
        if (!a.parentSessionId.isEmpty()) { hasHierarchy = true; break; }
    }
    if (hasHierarchy)
        sorted = buildHierarchicalOrder(sorted);

    // Rebuild parent map for collapse logic
    m_parentOf.clear();
    for (const auto &agent : sorted) {
        if (!agent.parentSessionId.isEmpty())
            m_parentOf[agent.sessionId] = agent.parentSessionId;
    }

    // Clean stale collapse state
    QSet<QString> validRoots;
    for (const auto &agent : sorted) {
        if (agent.isOrchestratorRoot)
            validRoots.insert(agent.sessionId);
    }
    m_collapsedRoots.intersect(validRoots);

    // Check if structure changed (adds/removes) vs. data-only update
    QSet<QString> incomingIds;
    for (const auto &agent : sorted)
        incomingIds.insert(agent.sessionId);

    bool structureChanged = (incomingIds.size() != m_cards.size());
    if (!structureChanged) {
        for (auto it = m_cards.constBegin(); it != m_cards.constEnd(); ++it) {
            if (!incomingIds.contains(it.key())) {
                structureChanged = true;
                break;
            }
        }
    }

    if (!structureChanged) {
        // Data-only update — update cards in-place, no layout rebuild
        for (const auto &agent : sorted) {
            if (auto *card = m_cards.value(agent.sessionId)) {
                card->update(agent);
                card->setSelected(agent.sessionId == selectedId);
                if (agent.isOrchestratorRoot)
                    card->setChildrenCollapsed(m_collapsedRoots.contains(agent.sessionId));
            }
        }
        // Reapply visibility for collapsed roots
        for (const QString &rootId : m_collapsedRoots)
            updateChildVisibility(rootId, false);
        return;
    }

    // Structure changed — full rebuild needed
    clearCards();

    QString lastDateGroup;
    QDate today = QDate::currentDate();
    auto &thm = ThemeManager::instance();

    for (const auto &agent : sorted) {
        // Day divider (skip for children of collapsed roots)
        if (!m_collapsed && agent.updatedAt > 0) {
            QDate date = QDateTime::fromSecsSinceEpoch(agent.updatedAt).date();
            QString dateGroup;
            if (date == today)
                dateGroup = "Today";
            else if (date == today.addDays(-1))
                dateGroup = "Yesterday";
            else
                dateGroup = QDateTime::fromSecsSinceEpoch(agent.updatedAt).toString("MMM d");

            if (dateGroup != lastDateGroup) {
                auto *divider = new QLabel(dateGroup, m_scrollContent);
                divider->setFixedHeight(30);
                divider->setContentsMargins(14, 12, 0, 4);
                QFont df = divider->font();
                df.setPixelSize(10);
                df.setWeight(QFont::DemiBold);
                df.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
                divider->setFont(df);
                bool isFirst = m_dividers.isEmpty();
                divider->setStyleSheet(isFirst
                    ? QStringLiteral("QLabel { color: %1; background: transparent; }")
                        .arg(thm.hex("text_muted"))
                    : QStringLiteral("QLabel { color: %1; background: transparent; "
                        "border-top: 1px solid %2; }")
                        .arg(thm.hex("text_muted"), thm.hex("border_subtle")));
                m_agentLayout->insertWidget(m_agentLayout->count() - 1, divider);
                m_dividers.append(divider);
                lastDateGroup = dateGroup;
            }
        }

        auto *card = new AgentCard(agent.sessionId, m_scrollContent);
        card->update(agent);
        card->setSelected(agent.sessionId == selectedId);
        card->setCollapsed(m_collapsed);

        if (agent.isOrchestratorRoot) {
            card->setChildrenCollapsed(m_collapsedRoots.contains(agent.sessionId));
            connect(card, &AgentCard::collapseToggled, this, [this](const QString &rootId, bool collapsed) {
                if (collapsed) {
                    m_collapsedRoots.insert(rootId);
                    // Auto-select root if selected child is being hidden
                    if (isDescendantOf(m_selectedId, rootId)) {
                        setSelectedAgent(rootId);
                        emit agentSelected(rootId);
                    }
                    updateChildVisibility(rootId, false);
                } else {
                    m_collapsedRoots.remove(rootId);
                    updateChildVisibility(rootId, true);
                }
            });
        }

        connect(card, &AgentCard::clicked, this, [this](const QString &sid) {
            setSelectedAgent(sid);
            emit agentSelected(sid);
        });
        connect(card, &AgentCard::deleteRequested, this, &AgentFleetPanel::deleteRequested);
        connect(card, &AgentCard::doubleClicked, this, &AgentFleetPanel::exportAndDeleteRequested);
        connect(card, &AgentCard::renameRequested, this, &AgentFleetPanel::renameRequested);
        connect(card, &AgentCard::favoriteToggled, this, &AgentFleetPanel::favoriteToggled);
        m_agentLayout->insertWidget(m_agentLayout->count() - 1, card);  // before stretch
        m_cards[agent.sessionId] = card;
    }

    // Apply collapse visibility after all cards are created
    for (const QString &rootId : m_collapsedRoots)
        updateChildVisibility(rootId, false);
}

void AgentFleetPanel::updateAgent(const AgentSummary &summary)
{
    if (auto *card = m_cards.value(summary.sessionId)) {
        // Preserve layout-computed fields (set by buildHierarchicalOrder during rebuild)
        AgentSummary patched = summary;
        patched.depth = card->depth();
        patched.isOrchestratorRoot = card->isOrchestratorRoot();
        patched.childCount = card->childCount();
        card->update(patched);
    }
}

void AgentFleetPanel::setSelectedAgent(const QString &sessionId)
{
    if (m_selectedId == sessionId) return;
    if (auto *old = m_cards.value(m_selectedId))
        old->setSelected(false);
    m_selectedId = sessionId;
    ensureVisible(sessionId);
    if (auto *cur = m_cards.value(sessionId))
        cur->setSelected(true);
}

void AgentFleetPanel::updateChildVisibility(const QString &rootId, bool visible)
{
    for (auto it = m_parentOf.constBegin(); it != m_parentOf.constEnd(); ++it) {
        QString current = it.key();
        QString parent = it.value();
        bool isDescendant = false;
        while (!parent.isEmpty()) {
            if (parent == rootId) {
                isDescendant = true;
                break;
            }
            parent = m_parentOf.value(parent);
        }
        if (isDescendant) {
            if (auto *card = m_cards.value(current))
                card->setVisible(visible);
        }
    }
}

bool AgentFleetPanel::isDescendantOf(const QString &sessionId, const QString &ancestorId) const
{
    QString current = sessionId;
    while (m_parentOf.contains(current)) {
        current = m_parentOf[current];
        if (current == ancestorId)
            return true;
    }
    return false;
}

void AgentFleetPanel::ensureVisible(const QString &sessionId)
{
    QString current = sessionId;
    while (m_parentOf.contains(current)) {
        QString parent = m_parentOf[current];
        if (m_collapsedRoots.contains(parent)) {
            m_collapsedRoots.remove(parent);
            updateChildVisibility(parent, true);
            if (auto *rootCard = m_cards.value(parent))
                rootCard->setChildrenCollapsed(false);
        }
        current = parent;
    }
}

void AgentFleetPanel::clearCards()
{
    for (auto *card : m_cards)
        card->deleteLater();
    m_cards.clear();

    for (auto *div : m_dividers)
        div->deleteLater();
    m_dividers.clear();
}

void AgentFleetPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), ThemeManager::instance().palette().surface0);
}

void AgentFleetPanel::applyThemeColors()
{
    auto &thm = ThemeManager::instance();

    m_header->setStyleSheet(QStringLiteral("background: %1;").arg(thm.hex("bg_raised")));

    m_headerLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background: transparent; }").arg(thm.hex("text_muted")));

    // Circular new agent button
    auto btnStyle = QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; "
        "border-radius: 13px; font-size: 16px; font-weight: 300; padding: 0px 0px 2px 0px; }"
        "QPushButton:hover { background: %2; color: %3; border: none; }")
        .arg(thm.hex("text_muted"), thm.hex("bg_raised"), thm.hex("text_primary"));
    m_newAgentBtn->setStyleSheet(btnStyle);
    m_newAgentBtn->setText("+");

    m_menuBtn->setStyleSheet(btnStyle);
    m_menuBtn->setText("\u22EF");  // midline horizontal ellipsis

    m_actionsMenu->setStyleSheet(QStringLiteral(
        "QMenu { background: %1; border: 1px solid %2; border-radius: 6px; padding: 4px 0; }"
        "QMenu::item { color: %3; padding: 6px 16px; font-size: 12px; }"
        "QMenu::item:selected { background: %4; }")
        .arg(thm.hex("bg_raised"), thm.hex("border_standard"),
             thm.hex("text_secondary"), thm.hex("surface1")));

    m_scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QWidget#scrollContent { background: transparent; }"));
    m_scrollArea->viewport()->setAutoFillBackground(false);
    m_scrollContent->setObjectName("scrollContent");
}
