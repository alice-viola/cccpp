#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QIcon>
#include <QMap>
#include <nlohmann/json.hpp>

#include "ui/AgentFleetPanel.h"  // for AgentSummary
#include "ui/EffectsPanel.h"     // for FileChange

struct MessageRecord;
class InputBar;
class ModeSelector;
class ModelSelector;
class ProfileSelector;
class ChatMessageWidget;
class ToolCallGroupWidget;
class ThinkingIndicator;
class ThinkingBlockWidget;
class SuggestionChips;
class ClaudeProcess;
class SessionManager;
class DiffEngine;
class Database;
class CodeViewer;

struct ChatTab {
    QWidget *container = nullptr;
    QScrollArea *scrollArea = nullptr;
    QVBoxLayout *messagesLayout = nullptr;
    ClaudeProcess *process = nullptr;
    ChatMessageWidget *currentAssistantMsg = nullptr;
    ToolCallGroupWidget *currentToolGroup = nullptr;
    ThinkingIndicator *thinkingIndicator = nullptr;
    ThinkingBlockWidget *currentThinkingBlock = nullptr;
    SuggestionChips *suggestionChips = nullptr;
    QWidget *welcomeWidget = nullptr;
    QString sessionId;
    QString pendingEditFile;
    QString accumulatedRawContent;
    QString pendingText;
    QTimer *textFlushTimer = nullptr;
    int turnId = 0;
    int tabIndex = -1;
    bool processing = false;
    bool hasFirstAssistantMsg = false;
    bool sessionConfirmed = false;
    bool hasPendingQuestion = false;
    bool unread = false;

    // Context usage tracking
    int totalInputTokens = 0;
    int totalOutputTokens = 0;
    int totalCacheReadTokens = 0;
    double totalCostUsd = 0.0;

    // Agent fleet tracking
    int editCount = 0;
    QString lastActivity;
    QStringList profileIds;
    QString overrideMode;  // if set, overrides the UI mode selector
    qint64 updatedAt = 0;
    bool favorite = false;
};

class ChatPanel : public QWidget {
    Q_OBJECT
public:
    explicit ChatPanel(QWidget *parent = nullptr);

    void setSessionManager(SessionManager *mgr);
    void setDiffEngine(DiffEngine *diff);
    void setDatabase(Database *db);
    void setWorkingDirectory(const QString &dir);
    void setCodeViewer(CodeViewer *viewer);

    QString newChat();
    void closeAllTabs();
    void restoreSession(const QString &sessionId);
    void sendMessage(const QString &text);

    void rewindToCheckpoint(const QString &checkpointUuid);
    void rewindCurrentTurn();

    InputBar *inputBar() const { return m_inputBar; }
    ModeSelector *modeSelector() const { return m_modeSelector; }
    ModelSelector *modelSelector() const { return m_modelSelector; }
    int tabCount() const { return m_tabs.size(); }
    QString currentSessionId() const;

    // Agent Fleet API
    void hideTabBar();
    QList<AgentSummary> agentSummaries() const;
    void selectSession(const QString &sessionId);
    QList<FileChange> extractFileChangesFromHistory(const QString &sessionId);
    QList<FileChange> extractFileChangesFromHistory(const QString &sessionId,
                                                     const QList<MessageRecord> &messages);
    QMap<int, qint64> turnTimestampsForSession(const QString &sessionId) const;
    QMap<int, qint64> turnTimestampsForSession(const QList<MessageRecord> &messages) const;
    AgentSummary agentSummaryForSession(const QString &sessionId) const;
    void scrollToTurn(int turnId);
    void exportChatHistory(const QString &sessionId);
    void deleteSession(const QString &sessionId);
    void deleteSessionNoConfirm(const QString &sessionId);
    void setSessionFavorite(const QString &sessionId, bool favorite);
    void renameSession(const QString &sessionId, const QString &title);

    // Delegation API
    QString delegateToChild(const QString &parentSessionId,
                            const QString &task,
                            const QString &context,
                            const QString &specialistProfileId = {},
                            const QStringList &extraProfileIds = {});
    void sendMessageToSession(const QString &sessionId, const QString &text);
    void configureSession(const QString &sessionId,
                          const QString &mode,
                          const QStringList &profileIds);
    QString sessionFinalOutput(const QString &sessionId) const;

signals:
    void fileChanged(const QString &filePath);
    void navigateToFile(const QString &filePath, int line);
    void planFileDetected(const QString &filePath);
    void aboutToSendMessage();
    void processingChanged(bool processing);
    void applyCodeRequested(const QString &code, const QString &language, const QString &filePath);
    void activeSessionChanged(const QString &sessionId);
    void editApplied(const QString &filePath, const QString &oldText,
                     const QString &newText, int startLine);
    void rewindCompleted(bool success);
    void inlineEditRequested(const QString &filePath, const QString &selectedCode,
                             const QString &instruction);

    // Delegation signals
    void childSessionCompleted(const QString &parentSessionId,
                               const QString &childSessionId,
                               const QString &output);
    void pipelineRequested(const QString &pipelineName, const QString &task);
    void orchestrateRequested(const QString &task, const QStringList &profileIds);
    void sessionIdChanged(const QString &oldId, const QString &newId);
    void sessionFinishedProcessing(const QString &sessionId);
    void mcpOrchestratorToolCalled(const QString &sessionId,
                                    const QString &toolName,
                                    const nlohmann::json &arguments);

    // Agent Fleet signals
    void sessionListChanged();
    void agentActivityChanged(const QString &sessionId, const QString &activity);
    void historicalEffectsReady(const QString &sessionId, const QList<FileChange> &changes);
    void turnStarted(const QString &sessionId, int turnId);
    void visibleTurnChanged(const QString &sessionId, int turnId);
    void turnTimestampsReady(const QString &sessionId, const QMap<int, qint64> &timestamps);

private slots:
    void onSendRequested(const QString &text);
    void onSlashCommand(const QString &command, const QString &args);
    void onRevertRequested(int turnId);
    void onToolFileClicked(const QString &filePath, const QString &searchText);
    void applyThemeColors();

private:
    ChatTab &currentTab();
    ChatTab *tabForProcess(ClaudeProcess *proc);
    void wireProcessSignals(ChatTab &tab);
    QWidget *createChatContent();
    void addMessageToTab(ChatTab &tab, ChatMessageWidget *msg);
    void scrollTabToBottom(ChatTab &tab);
    void flushPendingText(ChatTab &tab);
    void setTabProcessingState(ChatTab &tab, bool processing);
    void refreshInputBarForCurrentTab();
    void showHistoryMenu();
    QString buildInlineDiffHtml(const QString &filePath, const QString &oldStr, const QString &newStr);
    QString buildDiffMarkdown(const QString &filePath, const QString &oldStr, const QString &newStr);
    QString buildContextPreamble(const QString &userText);
    void updateInputBarContext();
    void showSuggestionChips(ChatTab &tab, const QString &responseText);
    void showAcceptAllButton(ChatTab &tab);
    void saveCurrentTextSegment(ChatTab &tab);
    int insertPosForTab(const ChatTab &tab) const;
    void removeMessagesAfterTurn(int turnId);
    void showPlansMenu();
    void updateStatsLabel();
    void updateTabIcon(int tabIndex);
    QIcon dotIcon(const QColor &color);

    QTabWidget *m_tabWidget;
    QPushButton *m_newChatBtn;
    QPushButton *m_historyBtn;
    QPushButton *m_plansBtn;
    InputBar *m_inputBar;
    ModeSelector *m_modeSelector;
    ModelSelector *m_modelSelector;
    ProfileSelector *m_profileSelector;
    QPushButton *m_orchestratorToggle;
    QLabel *m_statsLabel;
    QMap<int, ChatTab> m_tabs;

    SessionManager *m_sessionMgr = nullptr;
    DiffEngine *m_diffEngine = nullptr;
    Database *m_database = nullptr;
    CodeViewer *m_codeViewer = nullptr;
    QString m_workingDir;
    int m_pendingRevertTurnId = 0;
    int m_previousTabIndex = -1;
    QTimer *m_scrollDebounce = nullptr;
};
