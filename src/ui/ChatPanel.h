#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QMap>
#include <nlohmann/json.hpp>

class InputBar;
class ModeSelector;
class ChatMessageWidget;
class ToolCallGroupWidget;
class ClaudeProcess;
class SessionManager;
class SnapshotManager;
class DiffEngine;
class Database;

struct ChatTab {
    QWidget *container = nullptr;
    QScrollArea *scrollArea = nullptr;
    QVBoxLayout *messagesLayout = nullptr;
    ClaudeProcess *process = nullptr;
    ChatMessageWidget *currentAssistantMsg = nullptr;
    ToolCallGroupWidget *currentToolGroup = nullptr;
    QString sessionId;
    int turnId = 0;
    int tabIndex = -1;
};

class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget *parent = nullptr);

    void setSessionManager(SessionManager *mgr);
    void setSnapshotManager(SnapshotManager *snap);
    void setDiffEngine(DiffEngine *diff);
    void setDatabase(Database *db);
    void setWorkingDirectory(const QString &dir);

    QString newChat();
    void restoreSession(const QString &sessionId);
    void sendMessage(const QString &text);

    InputBar *inputBar() const { return m_inputBar; }
    ModeSelector *modeSelector() const { return m_modeSelector; }
    int tabCount() const { return m_tabs.size(); }

signals:
    void fileChanged(const QString &filePath);
    void navigateToFile(const QString &filePath, int line);
    void aboutToSendMessage();

private slots:
    void onSendRequested(const QString &text);
    void onRevertRequested(int turnId);

private:
    ChatTab &currentTab();
    ChatTab *tabForProcess(ClaudeProcess *proc);
    void wireProcessSignals(ChatTab &tab);
    QWidget *createChatContent();
    void addMessageToTab(ChatTab &tab, ChatMessageWidget *msg);
    void scrollTabToBottom(ChatTab &tab);
    void setProcessingState(bool processing);
    void showHistoryMenu();
    QString buildInlineDiffHtml(const QString &filePath, const QString &oldStr, const QString &newStr);

    QTabWidget *m_tabWidget;
    QPushButton *m_historyBtn;
    InputBar *m_inputBar;
    ModeSelector *m_modeSelector;
    QMap<int, ChatTab> m_tabs;

    SessionManager *m_sessionMgr = nullptr;
    SnapshotManager *m_snapshotMgr = nullptr;
    DiffEngine *m_diffEngine = nullptr;
    Database *m_database = nullptr;
    QString m_workingDir;
};
