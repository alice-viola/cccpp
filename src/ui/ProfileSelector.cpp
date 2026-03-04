#include "ui/ProfileSelector.h"
#include "ui/ThemeManager.h"
#include "core/PersonalityProfile.h"
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>

ProfileSelector::ProfileSelector(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_button = new QPushButton(this);
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setFixedHeight(24);
    layout->addWidget(m_button);

    connect(m_button, &QPushButton::clicked, this, &ProfileSelector::showProfileMenu);

    updateLabel();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this] { updateLabel(); });

    connect(&ProfileManager::instance(), &ProfileManager::profilesChanged,
            this, [this] { updateLabel(); });
}

void ProfileSelector::setSelectedIds(const QStringList &ids)
{
    if (m_selectedIds == ids) return;
    m_selectedIds = ids;
    updateLabel();
    emit selectionChanged(m_selectedIds);
}

void ProfileSelector::updateLabel()
{
    const auto &p = ThemeManager::instance().palette();

    QString text;
    if (m_selectedIds.isEmpty()) {
        text = QStringLiteral("Profiles \u25BE");
    } else if (m_selectedIds.size() == 1) {
        auto prof = ProfileManager::instance().profile(m_selectedIds.first());
        text = QStringLiteral("%1 \u25BE").arg(prof.name.isEmpty() ? m_selectedIds.first() : prof.name);
    } else {
        text = QStringLiteral("Profiles (%1) \u25BE").arg(m_selectedIds.size());
    }
    m_button->setText(text);

    QColor textColor = m_selectedIds.isEmpty() ? p.text_muted : p.teal;
    m_button->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; "
        "padding: 4px 10px; font-size: 12px; }"
        "QPushButton:hover { color: %2; }")
        .arg(textColor.name(), p.text_primary.name()));
}

void ProfileSelector::showProfileMenu()
{
    const auto &p = ThemeManager::instance().palette();
    auto profiles = ProfileManager::instance().allProfiles();

    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: %1; border: 1px solid %2; border-radius: 8px; padding: 4px; }"
        "QMenu::item { padding: 6px 20px 6px 12px; color: %3; border-radius: 4px; font-size: 12px; }"
        "QMenu::item:selected { background: %4; }"
        "QMenu::separator { height: 1px; background: %2; margin: 2px 8px; }")
        .arg(p.bg_surface.name(), p.border_standard.name(),
             p.text_primary.name(), p.bg_raised.name()));

    for (const auto &prof : profiles) {
        auto *action = menu.addAction(prof.name);
        action->setCheckable(true);
        action->setChecked(m_selectedIds.contains(prof.id));

        // Color indicator via icon
        QPixmap dot(10, 10);
        dot.fill(Qt::transparent);
        QPainter painter(&dot);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(prof.color);
        painter.drawEllipse(1, 1, 8, 8);
        action->setIcon(QIcon(dot));

        connect(action, &QAction::triggered, this, [this, id = prof.id](bool checked) {
            if (checked) {
                if (!m_selectedIds.contains(id))
                    m_selectedIds.append(id);
            } else {
                m_selectedIds.removeAll(id);
            }
            updateLabel();
            emit selectionChanged(m_selectedIds);
        });
    }

    menu.addSeparator();
    auto *manageAction = menu.addAction("Manage Profiles\u2026");
    connect(manageAction, &QAction::triggered, this, &ProfileSelector::manageProfilesRequested);

    QPoint pos = m_button->mapToGlobal(QPoint(0, 0));
    pos.setY(pos.y() - menu.sizeHint().height());
    menu.exec(pos);
}
