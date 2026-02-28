#pragma once

#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>

class ModeSelector : public QWidget {
    Q_OBJECT
public:
    explicit ModeSelector(QWidget *parent = nullptr);

    QString currentMode() const { return m_currentMode; }
    void setMode(const QString &mode);

signals:
    void modeChanged(const QString &mode);

private:
    void updateButtonStyles();

    QPushButton *m_agentBtn;
    QPushButton *m_askBtn;
    QPushButton *m_planBtn;
    QString m_currentMode = "agent";
};
