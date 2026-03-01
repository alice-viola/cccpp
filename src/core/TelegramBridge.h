#pragma once

#include <QObject>
#include <QMap>
#include <QTimer>

class TelegramApi;
class ClaudeProcess;
class SessionManager;
class Database;
class GitManager;
struct TelegramMessage;

struct TelegramSession {
    ClaudeProcess *process = nullptr;
    QString sessionId;
    QString workspace;
    qint64 chatId = 0;
    qint64 statusMessageId = 0;
    QString accumulatedText;
    QStringList toolSummary;
    QTimer *flushTimer = nullptr;
    bool processing = false;
};

class TelegramBridge : public QObject {
    Q_OBJECT
public:
    explicit TelegramBridge(TelegramApi *api, QObject *parent = nullptr);

    void setSessionManager(SessionManager *mgr);
    void setDatabase(Database *db);
    void setGitManager(GitManager *git);
    void setWorkingDirectory(const QString &dir);

private:
    void onMessage(const TelegramMessage &msg);

    // Command handlers
    void handleFreeText(TelegramSession &session, const QString &text);
    void handleStatus(TelegramSession &session);
    void handleNew(TelegramSession &session);
    void handleSessions(TelegramSession &session);
    void handleFiles(TelegramSession &session);
    void handleDiff(TelegramSession &session, const QString &args);
    void handleRevert(TelegramSession &session);
    void handleCommit(TelegramSession &session, const QString &args);
    void handleBranch(TelegramSession &session);
    void handleMode(TelegramSession &session, const QString &args);
    void handleCancel(TelegramSession &session);
    void handleHelp(TelegramSession &session);

    // Process management
    TelegramSession &getOrCreateSession(qint64 chatId);
    void wireProcessSignals(TelegramSession &session);
    void createNewProcess(TelegramSession &session);

    // Response accumulation
    void flushAccumulated(qint64 chatId);
    void sendFinalResponse(TelegramSession &session, const QString &text);

    // Helpers
    void send(qint64 chatId, const QString &text);
    static QString escapeMarkdown(const QString &text);

    TelegramApi *m_api = nullptr;
    SessionManager *m_sessionMgr = nullptr;
    Database *m_database = nullptr;
    GitManager *m_gitManager = nullptr;
    QString m_workingDir;

    QMap<qint64, TelegramSession> m_sessions; // chatId -> session
};
