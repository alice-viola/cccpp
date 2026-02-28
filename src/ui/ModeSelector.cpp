#include "ui/ModeSelector.h"
#include "ui/ThemeManager.h"

ModeSelector::ModeSelector(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(1);

    m_agentBtn = new QPushButton("Agent", this);
    m_askBtn   = new QPushButton("Ask",   this);
    m_planBtn  = new QPushButton("Plan",  this);

    m_agentBtn->setToolTip("Agent mode — Claude autonomously edits files and runs tools");
    m_askBtn->setToolTip("Ask mode — Conversational; Claude answers without making file changes");
    m_planBtn->setToolTip("Plan mode — Claude writes a plan and waits for your approval before acting");

    for (auto *btn : {m_agentBtn, m_askBtn, m_planBtn})
        btn->setFixedHeight(24);

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
    auto setStyle = [&p](QPushButton *btn, bool active) {
        if (active)
            btn->setStyleSheet(
                QStringLiteral(
                    "QPushButton { background: %1; color: %2; border: none; "
                    "padding: 2px 12px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
                    "QPushButton:hover { background: %3; }")
                .arg(p.green.name(), p.on_accent.name(), p.teal.name()));
        else
            btn->setStyleSheet(
                QStringLiteral(
                    "QPushButton { background: %1; color: %2; border: none; "
                    "padding: 2px 12px; border-radius: 4px; font-size: 11px; }"
                    "QPushButton:hover { background: %3; color: %4; }")
                .arg(p.bg_raised.name(), p.text_muted.name(),
                     p.hover_raised.name(), p.text_primary.name()));
    };
    setStyle(m_agentBtn, m_currentMode == "agent");
    setStyle(m_askBtn, m_currentMode == "ask");
    setStyle(m_planBtn, m_currentMode == "plan");
}
