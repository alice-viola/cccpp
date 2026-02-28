#pragma once
#include <QObject>
#include <QList>
#include "ui/ToastNotification.h"

// Singleton that creates and stacks ToastNotification widgets in the
// bottom-right corner of the application's main window.
//
// Usage:
//   ToastManager::instance().setParentWidget(mainWindow); // once, at startup
//   ToastManager::instance().show("Saved!", ToastType::Success);
class ToastManager : public QObject {
    Q_OBJECT
public:
    static ToastManager &instance();

    // Must be called once with the main window so toasts can be positioned.
    void setParentWidget(QWidget *parent);

    void show(const QString &message,
              ToastType type    = ToastType::Info,
              int durationMs    = 3000);

private:
    explicit ToastManager(QObject *parent = nullptr);

    // Reposition all active toasts stacked above the bottom-right corner.
    void reposition();

    QWidget *m_parent = nullptr;
    QList<ToastNotification *> m_active;
};
