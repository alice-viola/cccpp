#pragma once

#include <QObject>
#include <QString>
#include <QProcess>
#include <nlohmann/json.hpp>

class ChatPanel;

struct OrchestratorCommand {
    enum Action { Delegate, Validate, Done, Fail, Unknown };
    Action action = Unknown;
    QString role;           // for Delegate
    QString task;           // for Delegate
    QString command;        // for Validate
    QString description;    // for Validate
    QString summary;        // for Done
    QString reason;         // for Fail
};

class Orchestrator : public QObject {
    Q_OBJECT
public:
    explicit Orchestrator(QObject *parent = nullptr);

    void setChatPanel(ChatPanel *panel);
    void setWorkspace(const QString &workspace);

    QString start(const QString &goal,
                  const QStringList &contextProfileIds = {});
    void cancel();
    bool isRunning() const { return m_running; }

    QString sessionId() const { return m_sessionId; }
    QString currentPhase() const { return m_currentPhase; }
    int totalDelegations() const { return m_totalDelegations; }

    /// MCP server name used in tool names: mcp__<name>__<tool>
    static constexpr const char *MCP_SERVER_NAME = "c3p2-orchestrator";

    /// Install MCP server script and register with Claude CLI config.
    /// Safe to call multiple times — only writes if missing/outdated.
    static bool ensureMcpServerInstalled();

public slots:
    void onDelegateFinished(const QString &childSessionId, const QString &output);
    void onSessionIdChanged(const QString &oldId, const QString &newId);
    void onSessionFinishedProcessing(const QString &sessionId);
    void onMcpToolCalled(const QString &sessionId, const QString &toolName,
                         const nlohmann::json &arguments);

signals:
    void phaseChanged(const QString &phase);
    void delegationStarted(const QString &role, const QString &task);
    void validationStarted(const QString &command);
    void validationResult(bool passed, const QString &output);
    void retrying(const QString &reason, int attempt);
    void completed(const QString &summary);
    void failed(const QString &reason);
    void progressUpdate(const QString &message);

private slots:
    void onOrchestratorTurnFinished();

private:
    OrchestratorCommand parseCommand(const QString &assistantOutput) const;
    void executeCommand(const OrchestratorCommand &cmd);
    void feedResult(const QString &resultText);

    ChatPanel *m_chatPanel = nullptr;
    QString m_workspace;
    QString m_sessionId;
    QString m_goal;
    QString m_currentPhase;
    bool m_running = false;
    bool m_mcpCommandReceived = false;  // true when MCP tool call handled this turn

    int m_totalDelegations = 0;
    int m_totalRetries = 0;
    int m_stepAttempts = 0;

    static const int MAX_STEP_ATTEMPTS = 5;
    static const int MAX_TOTAL_DELEGATIONS = 20;

    QStringList m_contextProfileIds;  // user's personality profiles for specialists
    QString m_pendingChildSessionId;
    QProcess *m_validationProcess = nullptr;

    // Deferred feed: validation may finish before the Claude CLI process exits.
    // Store the result and send it once onSessionFinishedProcessing fires.
    bool m_orchestratorTurnDone = false;
    QString m_pendingFeedResult;
};
