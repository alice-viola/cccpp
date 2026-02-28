#pragma once

#include <QObject>
#include <QString>
#include <QList>
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

struct SnapshotRecord {
    int id = 0;
    QString sessionId;
    int turnId = 0;
    QString filePath;
    QByteArray content;
    QString gitStash;
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
    void deleteSession(const QString &sessionId);

    // Messages
    void saveMessage(const MessageRecord &msg);
    QList<MessageRecord> loadMessages(const QString &sessionId);
    void updateMessageSessionId(const QString &oldSessionId, const QString &newSessionId);

    // Snapshots
    void saveSnapshot(const SnapshotRecord &snap);
    QList<SnapshotRecord> loadSnapshots(const QString &sessionId, int turnId);
    void deleteSnapshots(const QString &sessionId, int turnId);

private:
    void createTables();
    QSqlDatabase m_db;
};
