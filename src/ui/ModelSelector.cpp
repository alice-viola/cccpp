#include "ui/ModelSelector.h"
#include "ui/ThemeManager.h"
#include <QHBoxLayout>
#include <QMenu>

ModelSelector::ModelSelector(QWidget *parent)
    : QWidget(parent)
{
    m_models = {
        {"claude-sonnet-4-6",           "Sonnet 4.6"},
        {"claude-opus-4-6",             "Opus 4.6"},
        {"claude-opus-4-5-20251101",    "Opus 4.5"},
        {"claude-haiku-4-5-20251001",   "Haiku 4.5"},
        {"claude-sonnet-4-5-20250929",  "Sonnet 4.5"},
    };

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_button = new QPushButton(this);
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setFixedHeight(24);
    layout->addWidget(m_button);

    connect(m_button, &QPushButton::clicked, this, &ModelSelector::showModelMenu);

    updateLabel();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this] { updateLabel(); });
}

QString ModelSelector::currentModelId() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_models.size())
        return m_models[m_currentIndex].id;
    return {};
}

QString ModelSelector::currentModelLabel() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_models.size())
        return m_models[m_currentIndex].label;
    return {};
}

void ModelSelector::setModel(const QString &modelId)
{
    for (int i = 0; i < m_models.size(); ++i) {
        if (m_models[i].id == modelId) {
            if (m_currentIndex == i) return;
            m_currentIndex = i;
            updateLabel();
            emit modelChanged(modelId);
            return;
        }
    }
}

void ModelSelector::updateLabel()
{
    const auto &p = ThemeManager::instance().palette();
    m_button->setText(QStringLiteral("%1 \u25BE").arg(currentModelLabel()));

    m_button->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; "
        "padding: 4px 10px; font-size: 12px; }"
        "QPushButton:hover { color: %2; }")
        .arg(p.text_muted.name(), p.text_primary.name()));
}

void ModelSelector::showModelMenu()
{
    const auto &p = ThemeManager::instance().palette();
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: %1; border: 1px solid %2; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 6px 20px 6px 12px; color: %3; border-radius: 4px; font-size: 12px; }"
        "QMenu::item:selected { background: %4; }"
        "QMenu::separator { height: 1px; background: %2; margin: 2px 8px; }")
        .arg(p.bg_surface.name(), p.border_standard.name(),
             p.text_primary.name(), p.bg_raised.name()));

    for (int i = 0; i < m_models.size(); ++i) {
        auto *action = menu.addAction(m_models[i].label);
        action->setCheckable(true);
        action->setChecked(i == m_currentIndex);
        connect(action, &QAction::triggered, this, [this, i] {
            m_currentIndex = i;
            updateLabel();
            emit modelChanged(m_models[i].id);
        });
    }

    QPoint pos = m_button->mapToGlobal(QPoint(0, 0));
    pos.setY(pos.y() - menu.sizeHint().height());
    menu.exec(pos);
}
