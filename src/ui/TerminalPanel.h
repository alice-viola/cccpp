#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>

class TerminalWidget;

class TerminalPanel : public QWidget {
    Q_OBJECT
public:
    explicit TerminalPanel(QWidget *parent = nullptr);

    void setWorkingDirectory(const QString &dir);
    TerminalWidget *newTerminal();
    void closeCurrentTerminal();
    void clearCurrentTerminal();
    int terminalCount() const;

signals:
    void visibilityToggled(bool visible);

private:
    void applyThemeColors();

    QWidget *m_headerWidget = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_newBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
    QTabWidget *m_tabWidget;
    QString m_workingDir;
};
