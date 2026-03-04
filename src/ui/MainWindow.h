#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QPushButton>
#include <QTabWidget>
#include <QLabel>
#include <QActionGroup>
#include <QVariantAnimation>
#include <QShowEvent>

class ClaudeProcess;

class WorkspaceTree;
class CodeViewer;
class ChatPanel;
class TerminalPanel;
class GitPanel;
class SearchPanel;
class AgentFleetPanel;
class EffectsPanel;
class SessionManager;
class DiffEngine;
class Database;
class GitManager;
class TelegramApi;
class TelegramBridge;
class DaemonClient;
class PipelineEngine;
class Orchestrator;

enum class ViewMode { Manager, Editor };

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void openWorkspace(const QString &path);

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

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
    void onInlineEdit();
    void onDaemonConnectionFailed();

private:
    void setupMenuBar();
    void setupUI();
    void setupStatusBar();
    void loadStylesheet();
    void applyThemeColors();
    void restoreSessions();
    void updateToggleButtons();
    void connectGitSignals();
    void syncEditorContextToChat();
    void setupTelegram();
    void animateSplitterSizes(const QList<int> &targetSizes, int durationMs = 200);
    void executeInlineEdit(const QString &filePath, const QString &selectedCode,
                           const QString &instruction, int startLine, int endLine,
                           const QString &modelId);

    // Mission Control
    void switchToMode(ViewMode mode);
    void rebuildFleetPanel();
    void wireEffectsPanel();
    void showFileBar(const QString &fileName);
    void dismissInlineFilePreview();

    ViewMode m_viewMode = ViewMode::Manager;

    QPushButton *m_toggleMode = nullptr;
    QPushButton *m_toggleAgents = nullptr;
    QPushButton *m_toggleEffects = nullptr;
    QPushButton *m_toggleTree = nullptr;
    QPushButton *m_toggleChat = nullptr;
    QWidget *m_fileBar = nullptr;
    QLabel *m_fileBarLabel = nullptr;
    QSplitter *m_splitter;
    QSplitter *m_centerSplitter;
    QTabWidget *m_leftTabs;
    WorkspaceTree *m_workspaceTree;
    CodeViewer *m_codeViewer;
    TerminalPanel *m_terminalPanel;
    ChatPanel *m_chatPanel;
    GitPanel *m_gitPanel;
    SearchPanel *m_searchPanel;

    // Mission Control panels
    AgentFleetPanel *m_agentFleet = nullptr;
    EffectsPanel *m_effectsPanel = nullptr;

    SessionManager *m_sessionMgr;
    DiffEngine *m_diffEngine;
    Database *m_database;
    GitManager *m_gitManager;
    TelegramApi *m_telegramApi = nullptr;
    TelegramBridge *m_telegramBridge = nullptr;
    DaemonClient *m_daemonClient = nullptr;
    PipelineEngine *m_pipelineEngine = nullptr;
    Orchestrator *m_orchestrator = nullptr;

    QString m_workspacePath;

    QLabel *m_statusFile       = nullptr;
    QLabel *m_statusBranch     = nullptr;
    QLabel *m_statusModel      = nullptr;
    QLabel *m_statusProcessing = nullptr;

    QActionGroup *m_themeGroup = nullptr;

    QVariantAnimation *m_splitterAnim = nullptr;

    // Cmd+K inline edit — dedicated process (never touches chat)
    ClaudeProcess *m_inlineEditProcess = nullptr;
    QString        m_inlineEditFile;
    QString        m_inlineEditOriginalContent;
};
