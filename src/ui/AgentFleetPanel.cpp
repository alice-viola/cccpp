#include "ui/AgentFleetPanel.h"
#include "ui/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
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
}

void AgentCard::update(const AgentSummary &summary)
{
    m_title = summary.title.isEmpty() ? "New Agent" : summary.title;
    m_activity = summary.activity;
    m_processing = summary.processing;
    m_blocked = summary.hasPendingQuestion;
    m_unread = summary.unread;
    m_editCount = summary.editCount;
    m_turnCount = summary.turnCount;
    m_costUsd = summary.costUsd;
    m_updatedAt = summary.updatedAt;
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
    return {200, 48};
}

QSize AgentCard::minimumSizeHint() const
{
    return m_collapsed ? QSize(36, 36) : QSize(100, 44);
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

    QColor tintColor = m_processing ? pal.green : pal.mauve;

    // Intensity wash (subtle background tint)
    if (intensity > 0.01f && !m_collapsed) {
        QPainterPath washPath;
        washPath.addRoundedRect(rect().adjusted(2, 1, -2, -1), 6, 6);
        QColor wash = tintColor;
        wash.setAlpha(static_cast<int>(intensity * 61));
        p.fillPath(washPath, wash);
    }

    // Selection/hover background on top
    QColor bg = Qt::transparent;
    if (m_selected)
        bg = pal.bg_raised;
    else if (m_hovered)
        bg = QColor(pal.text_primary.red(), pal.text_primary.green(),
                     pal.text_primary.blue(), 8);

    if (bg != Qt::transparent) {
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(2, 1, -2, -1), 6, 6);
        p.fillPath(path, bg);
    }

    // Selected accent bar
    if (m_selected) {
        p.setPen(Qt::NoPen);
        p.setBrush(pal.mauve);
        p.drawRoundedRect(QRect(0, 6, 3, height() - 12), 1.5, 1.5);
    }

    // Status dot color
    QColor dotColor;
    if (m_blocked)
        dotColor = pal.yellow;
    else if (m_processing)
        dotColor = pal.green;
    else
        dotColor = pal.overlay0;

    if (m_processing)
        dotColor.setAlphaF(0.4 + 0.6 * m_pulsePhase);

    if (m_collapsed) {
        // Collapsed mode: centered dot, size varies with intensity
        float dotR = 5.0f + intensity * 2.0f;
        QPointF center(width() / 2.0, height() / 2.0);
        p.setPen(Qt::NoPen);
        p.setBrush(dotColor);
        p.drawEllipse(center, dotR, dotR);

        if (m_unread) {
            p.setPen(QPen(pal.mauve, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(center, dotR + 3, dotR + 3);
        }
        return;
    }

    // ─── Expanded mode ───
    int leftPad = 14;
    int textRight = width() - 10;

    // Title line
    QFont titleFont = font();
    titleFont.setPixelSize(12);
    titleFont.setWeight(QFont::Medium);
    p.setFont(titleFont);
    p.setPen(m_selected ? pal.text_primary : pal.text_secondary);

    // Status dot before title
    int dotX = leftPad;
    int titleY = 18;
    float dotR = 3.5f;
    p.setPen(Qt::NoPen);
    p.setBrush(dotColor);
    p.drawEllipse(QPointF(dotX, titleY - 1), dotR, dotR);

    int textX = dotX + 12;

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
    p.setFont(titleFont);
    p.setPen(m_selected ? pal.text_primary : pal.text_secondary);
    QString elidedTitle = p.fontMetrics().elidedText(m_title, Qt::ElideRight, textRight - textX);
    p.drawText(textX, titleY, elidedTitle);

    // Date line
    if (m_updatedAt > 0) {
        QFont dateFont = font();
        dateFont.setPixelSize(10);
        p.setFont(dateFont);
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

        p.drawText(textX, 36, dateStr);
    }

    // Processing activity indicator
    if (m_processing && !m_activity.isEmpty()) {
        QFont actFont = font();
        actFont.setPixelSize(10);
        p.setFont(actFont);
        p.setPen(pal.green);
        int actW = width() - textX - 10;
        QString elidedAct = p.fontMetrics().elidedText(m_activity, Qt::ElideRight, actW);
        p.drawText(textX, 36, elidedAct);
    }

    // Unread dot
    if (m_unread && !m_selected) {
        p.setPen(Qt::NoPen);
        p.setBrush(pal.mauve);
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
        emit clicked(m_sessionId);
    }
    QWidget::mousePressEvent(event);
}

void AgentCard::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit doubleClicked(m_sessionId);
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
    m_header->setFixedHeight(38);
    auto *headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(12, 0, 4, 0);

    m_headerLabel = new QLabel("AGENTS", m_header);
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

    headerLayout->addWidget(m_headerLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_newAgentBtn);
    m_mainLayout->addWidget(m_header);

    // Scroll area for agent cards
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_scrollContent = new QWidget(m_scrollArea);
    m_agentLayout = new QVBoxLayout(m_scrollContent);
    m_agentLayout->setContentsMargins(4, 4, 4, 4);
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

void AgentFleetPanel::rebuild(const QList<AgentSummary> &agents, const QString &selectedId)
{
    clearCards();
    m_selectedId = selectedId;

    // Sort by updatedAt descending (most recent first)
    auto sorted = agents;
    std::sort(sorted.begin(), sorted.end(), [](const AgentSummary &a, const AgentSummary &b) {
        return a.updatedAt > b.updatedAt;
    });

    QString lastDateGroup;
    QDate today = QDate::currentDate();
    auto &thm = ThemeManager::instance();

    for (const auto &agent : sorted) {
        // Day divider
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
                divider->setFixedHeight(24);
                divider->setContentsMargins(14, 8, 0, 2);
                QFont df = divider->font();
                df.setPixelSize(10);
                df.setWeight(QFont::DemiBold);
                df.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
                divider->setFont(df);
                divider->setStyleSheet(QStringLiteral(
                    "QLabel { color: %1; background: transparent; }")
                    .arg(thm.hex("text_faint")));
                m_agentLayout->insertWidget(m_agentLayout->count() - 1, divider);
                m_dividers.append(divider);
                lastDateGroup = dateGroup;
            }
        }

        auto *card = new AgentCard(agent.sessionId, m_scrollContent);
        card->update(agent);
        card->setSelected(agent.sessionId == selectedId);
        card->setCollapsed(m_collapsed);
        connect(card, &AgentCard::clicked, this, [this](const QString &sid) {
            setSelectedAgent(sid);
            emit agentSelected(sid);
        });
        connect(card, &AgentCard::deleteRequested, this, &AgentFleetPanel::deleteRequested);
        connect(card, &AgentCard::doubleClicked, this, &AgentFleetPanel::exportAndDeleteRequested);
        m_agentLayout->insertWidget(m_agentLayout->count() - 1, card);  // before stretch
        m_cards[agent.sessionId] = card;
    }
}

void AgentFleetPanel::updateAgent(const AgentSummary &summary)
{
    if (auto *card = m_cards.value(summary.sessionId)) {
        card->update(summary);
    }
}

void AgentFleetPanel::setSelectedAgent(const QString &sessionId)
{
    if (m_selectedId == sessionId) return;
    if (auto *old = m_cards.value(m_selectedId))
        old->setSelected(false);
    m_selectedId = sessionId;
    if (auto *cur = m_cards.value(sessionId))
        cur->setSelected(true);
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

void AgentFleetPanel::applyThemeColors()
{
    auto &thm = ThemeManager::instance();

    setStyleSheet(QStringLiteral(
        "AgentFleetPanel { background: %1; }")
        .arg(thm.hex("bg_window")));

    m_headerLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background: transparent; }").arg(thm.hex("text_muted")));

    // Circular new agent button
    auto btnStyle = QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: 1px solid %2; "
        "border-radius: 13px; font-size: 16px; font-weight: 300; padding: 0px 0px 2px 0px; }"
        "QPushButton:hover { background: %3; color: %4; border-color: %4; }")
        .arg(thm.hex("text_muted"), thm.hex("border_subtle"),
             thm.hex("bg_raised"), thm.hex("text_primary"));
    m_newAgentBtn->setStyleSheet(btnStyle);
    m_newAgentBtn->setText("+");

    m_scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QWidget#scrollContent { background: transparent; }"));
    m_scrollContent->setObjectName("scrollContent");
}
