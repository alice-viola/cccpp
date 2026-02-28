#include "ui/ModeSelector.h"
#include "ui/ThemeManager.h"

ModeSelector::ModeSelector(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 6, 16, 6);
    layout->setSpacing(1);

    m_agentBtn = new QPushButton("Agent", this);
    m_askBtn   = new QPushButton("Ask",   this);
    m_planBtn  = new QPushButton("Plan",  this);

    m_agentBtn->setToolTip("Agent mode — Claude autonomously edits files and runs tools");
    m_askBtn->setToolTip("Ask mode — Conversational; Claude answers without making file changes");
    m_planBtn->setToolTip("Plan mode — Claude writes a plan and waits for your approval before acting");

    for (auto *btn : {m_agentBtn, m_askBtn, m_planBtn})
        btn->setFixedHeight(28);

    layout->addWidget(m_agentBtn);
    layout->addWidget(m_askBtn);
    layout->addWidget(m_planBtn);

    connect(m_agentBtn, &QPushButton::clicked, this, [this] { setMode("agent"); });
    connect(m_askBtn, &QPushButton::clicked, this, [this] { setMode("ask"); });
    connect(m_planBtn, &QPushButton::clicked, this, [this] { setMode("plan"); });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this] { updateButtonStyles(); });

    updateButtonStyles();
}

void ModeSelector::setMode(const QString &mode)
{
    if (m_currentMode == mode)
        return;
    m_currentMode = mode;
    updateButtonStyles();
    emit modeChanged(mode);
}

void ModeSelector::updateButtonStyles()
{
    const auto &p = ThemeManager::instance().palette();

    setStyleSheet(
        QStringLiteral("ModeSelector { background: %1; border-radius: 8px; padding: 2px; }")
        .arg(p.bg_raised.name()));

    auto setStyle = [&p](QPushButton *btn, bool active) {
        if (active)
            btn->setStyleSheet(
                QStringLiteral(
                    "QPushButton { background: %1; color: %2; border: none; "
                    "padding: 4px 14px; border-radius: 6px; font-weight: 600; font-size: 12px; }"
                    "QPushButton:hover { background: %3; }")
                .arg(p.blue.name(), p.on_accent.name(), p.lavender.name()));
        else
            btn->setStyleSheet(
                QStringLiteral(
                    "QPushButton { background: transparent; color: %1; border: none; "
                    "padding: 4px 14px; border-radius: 6px; font-size: 12px; }"
                    "QPushButton:hover { background: %2; color: %3; }")
                .arg(p.text_muted.name(), p.hover_raised.name(), p.text_primary.name()));
    };
    setStyle(m_agentBtn, m_currentMode == "agent");
    setStyle(m_askBtn, m_currentMode == "ask");
    setStyle(m_planBtn, m_currentMode == "plan");
}
