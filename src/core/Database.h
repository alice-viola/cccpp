#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QSqlDatabase>

struct SessionInfo;

struct MessageRecord {
    int id = 0;
    QString sessionId;
    QString role; // "user", "assistant", "tool"
    QString content;
    QString toolName;
    QString toolInput;
    int turnId = 0;
    qint64 timestamp = 0;
};

struct CheckpointRecord {
    QString sessionId;
    int turnId = 0;
    QString uuid;
    qint64 timestamp = 0;
};

class Database : public QObject {
    Q_OBJECT
public:
    explicit Database(QObject *parent = nullptr);
    ~Database();

    bool open(const QString &path = {});
    void close();

    // Sessions
    void saveSession(const SessionInfo &info);
    QList<SessionInfo> loadSessions();
    SessionInfo loadSession(const QString &sessionId);
    void deleteSession(const QString &sessionId);
    void deleteStalePendingSessions();

    // Messages
    void saveMessage(const MessageRecord &msg);
    QList<MessageRecord> loadMessages(const QString &sessionId);
    int turnCountForSession(const QString &sessionId);
    QMap<QString, int> turnCountsForSessions(const QStringList &sessionIds);
    void updateMessageSessionId(const QString &oldSessionId, const QString &newSessionId);

    // Checkpoints (CLI-backed, stores only the UUID per turn)
    void saveCheckpoint(const CheckpointRecord &cp);
    QList<CheckpointRecord> loadCheckpoints(const QString &sessionId);
    QString checkpointUuid(const QString &sessionId, int turnId);

private:
    void createTables();
    QSqlDatabase m_db;
};
