#pragma once

#include <QObject>
#include <QMap>
#include <QTimer>
#include <QJsonArray>
#include <functional>

class ClaudeProcess;
class SessionManager;
class Database;
class GitManager;

struct ChatSession {
    ClaudeProcess *process = nullptr;
    QString sessionId;
    qint64 chatId = 0;
    qint64 statusMessageId = 0;
    QString accumulatedText;
    QStringList toolSummary;
    QTimer *flushTimer = nullptr;
    int turnId = 0;
    bool processing = false;
    bool titleSet = false;
    bool responseSent = false;
};

class ChatHandler : public QObject {
    Q_OBJECT
public:
    explicit ChatHandler(QObject *parent = nullptr);

    void setSessionManager(SessionManager *mgr);
    void setDatabase(Database *db);
    void setGitManager(GitManager *git);
    void setWorkingDirectory(const QString &dir);

signals:
    void filesChanged();

protected:
    // Subclasses implement these four send primitives
    virtual void doSendResponse(qint64 chatId, const QString &text) = 0;
    virtual void doSendEditResponse(qint64 chatId, qint64 msgId, const QString &text) = 0;
    virtual void doSendWithKeyboard(qint64 chatId, const QString &text,
                                    const QJsonArray &rows) = 0;
    virtual void doRequestSendMessage(qint64 chatId, const QString &text,
                                      std::function<void(qint64)> cb) = 0;

    // Subclasses call these entry points when they receive input
    void onChatMessage(qint64 chatId, const QString &text);
    void onChatCommand(qint64 chatId, const QString &cmd, const QString &args);
    void onChatCallback(qint64 chatId, const QString &queryId, const QString &data);

    QString m_workingDir;
    SessionManager *m_sessionMgr = nullptr;
    Database *m_database = nullptr;
    GitManager *m_gitManager = nullptr;

private:
    ChatSession &getOrCreateSession(qint64 chatId);
    void wireProcessSignals(ChatSession &session);
    void createNewProcess(ChatSession &session);
    void flushAccumulated(qint64 chatId);
    void sendFinalResponse(ChatSession &session, const QString &text);

    void handleFreeText(ChatSession &session, const QString &text);
    void handleStatus(ChatSession &session);
    void handleNew(ChatSession &session);
    void handleSessions(ChatSession &session);
    void handleResume(ChatSession &session, const QString &sessionId);
    void handleFiles(ChatSession &session);
    void handleDiff(ChatSession &session, const QString &args);
    void handleRevert(ChatSession &session);
    void handleCommit(ChatSession &session, const QString &args);
    void handleBranch(ChatSession &session);
    void handleMode(ChatSession &session, const QString &args);
    void handleCancel(ChatSession &session);
    void handleHelp(ChatSession &session);

    QMap<qint64, ChatSession> m_sessions;
    static constexpr int kFlushIntervalMs = 2000;
};
