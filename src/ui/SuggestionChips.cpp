#include "ui/SuggestionChips.h"
#include "ui/ThemeManager.h"

SuggestionChips::SuggestionChips(QWidget *parent)
    : QFrame(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(6);
    m_layout->addStretch();

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &SuggestionChips::applyThemeColors);
}

void SuggestionChips::applyThemeColors()
{
    setStyleSheet(QStringLiteral(
        "SuggestionChips { background: transparent; border: none; }"));
}

void SuggestionChips::setSuggestions(const QStringList &suggestions)
{
    clear();

    auto &p = ThemeManager::instance().palette();
    int insertPos = 0;
    for (const QString &text : suggestions) {
        if (text.trimmed().isEmpty()) continue;
        auto *btn = new QPushButton(text, this);
        btn->setFixedHeight(24);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 12px; font-size: 11px; padding: 0 10px; }"
            "QPushButton:hover { background: %4; color: %5; border-color: %6; }")
            .arg(p.bg_surface.name(), p.text_secondary.name(), p.border_standard.name(),
                 p.bg_raised.name(), p.text_primary.name(), p.blue.name()));
        connect(btn, &QPushButton::clicked, this, [this, text] {
            emit suggestionClicked(text);
        });
        m_layout->insertWidget(insertPos++, btn);
    }

    setVisible(!suggestions.isEmpty());
}

void SuggestionChips::clear()
{
    QLayoutItem *child;
    while (m_layout->count() > 1) {
        child = m_layout->takeAt(0);
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    setVisible(false);
}
