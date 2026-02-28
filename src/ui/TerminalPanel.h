#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QPushButton>

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
    QTabWidget *m_tabWidget;
    QString m_workingDir;
};
