#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QToolBar>
#include <QPushButton>
#include <QTabWidget>
#include <QLabel>
#include <QActionGroup>

class WorkspaceTree;
class CodeViewer;
class ChatPanel;
class TerminalPanel;
class GitPanel;
class SearchPanel;
class SessionManager;
class SnapshotManager;
class DiffEngine;
class Database;
class GitManager;

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
    void onGitRefresh();
    void onThemeChanged(const QString &name);

private:
    void setupMenuBar();
    void setupToolBar();
    void setupUI();
    void setupStatusBar();
    void loadStylesheet();
    void applyThemeColors();
    void restoreSessions();
    void updateToggleButtons();
    void connectGitSignals();

    QToolBar *m_toolBar = nullptr;
    QPushButton *m_toggleTree = nullptr;
    QPushButton *m_toggleEditor = nullptr;
    QPushButton *m_toggleChat = nullptr;
    QPushButton *m_toggleTerminal = nullptr;
    QSplitter *m_splitter;
    QSplitter *m_centerSplitter;
    QTabWidget *m_leftTabs;
    WorkspaceTree *m_workspaceTree;
    CodeViewer *m_codeViewer;
    TerminalPanel *m_terminalPanel;
    ChatPanel *m_chatPanel;
    GitPanel *m_gitPanel;
    SearchPanel *m_searchPanel;

    SessionManager *m_sessionMgr;
    SnapshotManager *m_snapshotMgr;
    DiffEngine *m_diffEngine;
    Database *m_database;
    GitManager *m_gitManager;

    QString m_workspacePath;

    // Status bar labels (right-to-left: model | branch | processing | file)
    QLabel *m_statusFile       = nullptr;
    QLabel *m_statusBranch     = nullptr;
    QLabel *m_statusModel      = nullptr;
    QLabel *m_statusProcessing = nullptr;

    // Theme menu actions
    QActionGroup *m_themeGroup = nullptr;
};
