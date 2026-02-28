#include "ui/ToastManager.h"
#include <QRect>

ToastManager &ToastManager::instance()
{
    static ToastManager inst;
    return inst;
}

ToastManager::ToastManager(QObject *parent) : QObject(parent) {}

void ToastManager::setParentWidget(QWidget *parent)
{
    m_parent = parent;
}

void ToastManager::show(const QString &message, ToastType type, int durationMs)
{
    if (!m_parent) return;

    auto *toast = new ToastNotification(message, type);
    m_active.append(toast);

    connect(toast, &ToastNotification::dismissed, this, [this, toast] {
        m_active.removeOne(toast);
        reposition();
    });

    reposition();
    toast->showToast(durationMs);
}

void ToastManager::reposition()
{
    if (!m_parent) return;

    // Use screen-space geometry of the parent window
    QRect parentRect = m_parent->frameGeometry();
    constexpr int margin  = 16;
    constexpr int spacing = 8;

    int y = parentRect.bottom() - margin;

    for (auto it = m_active.rbegin(); it != m_active.rend(); ++it) {
        auto *toast = *it;
        QSize sz = toast->sizeHint();
        int x = parentRect.right() - sz.width() - margin;
        y -= sz.height();
        toast->move(x, y);
        y -= spacing;
    }
}
