#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QToolBar>
#include <QPushButton>

class WorkspaceTree;
class CodeViewer;
class ChatPanel;
class TerminalPanel;
class SessionManager;
class SnapshotManager;
class DiffEngine;
class Database;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void openWorkspace(const QString &path);

private slots:
    void onFileSelected(const QString &filePath);
    void onFileChanged(const QString &filePath);
    void onNewChat();
    void onOpenWorkspace();
    void onSaveFile();
    void onSaveAllFiles();
    void onBeforeTurnBegins();
    void onToggleTerminal();
    void onNewTerminal();
    void onClearTerminal();

private:
    void setupMenuBar();
    void setupToolBar();
    void setupUI();
    void loadStylesheet();
    void restoreSessions();
    void updateToggleButtons();

    QToolBar *m_toolBar = nullptr;
    QPushButton *m_toggleTree = nullptr;
    QPushButton *m_toggleEditor = nullptr;
    QPushButton *m_toggleChat = nullptr;
    QPushButton *m_toggleTerminal = nullptr;
    QSplitter *m_splitter;
    QSplitter *m_centerSplitter;
    WorkspaceTree *m_workspaceTree;
    CodeViewer *m_codeViewer;
    TerminalPanel *m_terminalPanel;
    ChatPanel *m_chatPanel;

    SessionManager *m_sessionMgr;
    SnapshotManager *m_snapshotMgr;
    DiffEngine *m_diffEngine;
    Database *m_database;

    QString m_workspacePath;
};
