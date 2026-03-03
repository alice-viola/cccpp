#include "ui/ModeSelector.h"
#include "ui/ThemeManager.h"
#include <QHBoxLayout>
#include <QMenu>

ModeSelector::ModeSelector(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_button = new QPushButton(this);
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setFixedHeight(24);
    layout->addWidget(m_button);

    connect(m_button, &QPushButton::clicked, this, &ModeSelector::showModeMenu);

    updateLabel();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this] { updateLabel(); });
}

void ModeSelector::setMode(const QString &mode)
{
    if (m_currentMode == mode) return;
    m_currentMode = mode;
    updateLabel();
    emit modeChanged(mode);
}

void ModeSelector::updateLabel()
{
    const auto &p = ThemeManager::instance().palette();
    QString label = m_currentMode.at(0).toUpper() + m_currentMode.mid(1);
    m_button->setText(QStringLiteral("\u221E %1 \u25BE").arg(label));

    m_button->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 12px; padding: 4px 12px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background: %4; border-color: %5; }")
        .arg(p.bg_raised.name(), p.text_primary.name(), p.border_subtle.name(),
             p.hover_raised.name(), p.border_standard.name()));
}

void ModeSelector::showModeMenu()
{
    const auto &p = ThemeManager::instance().palette();
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: %1; border: 1px solid %2; border-radius: 8px; padding: 4px; }"
        "QMenu::item { padding: 6px 20px 6px 12px; color: %3; border-radius: 4px; font-size: 12px; }"
        "QMenu::item:selected { background: %4; }"
        "QMenu::separator { height: 1px; background: %2; margin: 2px 8px; }")
        .arg(p.bg_surface.name(), p.border_standard.name(),
             p.text_primary.name(), p.bg_raised.name()));

    struct ModeEntry { QString key; QString label; };
    for (const auto &entry : std::initializer_list<ModeEntry>{
            {"agent", "Agent"}, {"ask", "Ask"}, {"plan", "Plan"}}) {
        auto *action = menu.addAction(entry.label);
        action->setCheckable(true);
        action->setChecked(entry.key == m_currentMode);
        connect(action, &QAction::triggered, this, [this, key = entry.key] {
            setMode(key);
        });
    }

    QPoint pos = m_button->mapToGlobal(QPoint(0, 0));
    pos.setY(pos.y() - menu.sizeHint().height());
    menu.exec(pos);
}
