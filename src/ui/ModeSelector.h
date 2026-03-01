#pragma once

#include <QWidget>
#include <QPushButton>

class ModeSelector : public QWidget {
    Q_OBJECT
public:
    explicit ModeSelector(QWidget *parent = nullptr);

    QString currentMode() const { return m_currentMode; }
    void setMode(const QString &mode);

signals:
    void modeChanged(const QString &mode);

private:
    void updateLabel();
    void showModeMenu();

    QPushButton *m_button;
    QString m_currentMode = "agent";
};
