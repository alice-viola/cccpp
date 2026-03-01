#pragma once

#include <QObject>
#include <QLocalSocket>
#include <QTimer>
#include <QMap>
#include <QJsonArray>

class ClaudeProcess;
class StreamParser;
class SessionManager;
class Database;
class GitManager;

struct DaemonChatSession {
    ClaudeProcess *process = nullptr;
    QString sessionId;
    qint64 chatId = 0;
    qint64 statusMessageId = 0;
    QString accumulatedText;
    QStringList toolSummary;
    QTimer *flushTimer = nullptr;
    bool processing = false;
    bool titleSet = false;
};

class DaemonClient : public QObject {
    Q_OBJECT
public:
    explicit DaemonClient(QObject *parent = nullptr);
    ~DaemonClient();

    void setSessionManager(SessionManager *mgr);
    void setDatabase(Database *db);
    void setGitManager(GitManager *git);
    void setWorkingDirectory(const QString &dir);

    bool connectToDaemon();
    void registerWorkspace(const QString &workspace, const QString &name = {});
    void unregisterWorkspace();

    bool isConnected() const;

    static bool spawnDaemon();

signals:
    void connected();
    void disconnected();
    void connectionFailed();

private:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void processMessage(const QByteArray &line);

    // Command handling (mirrors TelegramBridge)
    void handleFreeText(DaemonChatSession &session, const QString &text);
    void handleCommand(DaemonChatSession &session, const QString &cmd, const QString &args);
    void handleStatus(DaemonChatSession &session);
    void handleNew(DaemonChatSession &session);
    void handleSessions(DaemonChatSession &session);
    void handleResume(DaemonChatSession &session, const QString &sessionId);
    void handleCallbackQuery(DaemonChatSession &session, const QString &queryId, const QString &data);
    void handleFiles(DaemonChatSession &session);
    void handleDiff(DaemonChatSession &session, const QString &args);
    void handleRevert(DaemonChatSession &session);
    void handleCommit(DaemonChatSession &session, const QString &args);
    void handleBranch(DaemonChatSession &session);
    void handleMode(DaemonChatSession &session, const QString &args);
    void handleCancel(DaemonChatSession &session);
    void handleHelp(DaemonChatSession &session);

    // Process management
    DaemonChatSession &getOrCreateSession(qint64 chatId);
    void wireProcessSignals(DaemonChatSession &session);
    void createNewProcess(DaemonChatSession &session);

    // Response accumulation
    void flushAccumulated(qint64 chatId);
    void sendFinalResponse(DaemonChatSession &session, const QString &text);

    // IPC helpers
    void sendToDaemon(const QByteArray &data);
    void sendResponse(qint64 chatId, const QString &text);
    void sendEditResponse(qint64 chatId, qint64 messageId, const QString &text);
    void sendWithKeyboard(qint64 chatId, const QString &text, const QJsonArray &keyboard);
    void requestSendMessage(qint64 chatId, const QString &text,
                            std::function<void(qint64 messageId)> callback);

    QLocalSocket *m_socket = nullptr;
    QByteArray m_readBuffer;
    QString m_workingDir;
    QString m_workspaceName;

    SessionManager *m_sessionMgr = nullptr;
    Database *m_database = nullptr;
    GitManager *m_gitManager = nullptr;

    QMap<qint64, DaemonChatSession> m_sessions; // chatId -> session
    QMap<QString, std::function<void(qint64)>> m_pendingMessageIds; // requestId -> callback

    static const int kFlushIntervalMs = 2000;
};
