#include "core/Database.h"
#include "core/SessionManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>

Database::Database(QObject *parent)
    : QObject(parent)
{
}

Database::~Database()
{
    close();
}

bool Database::open(const QString &path)
{
    QString dbPath = path;
    if (dbPath.isEmpty()) {
        QString configDir = QDir::homePath() + "/.cccpp";
        QDir().mkpath(configDir);
        dbPath = configDir + "/history.db";
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE", "cccpp_main");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open())
        return false;

    createTables();
    return true;
}

void Database::close()
{
    if (m_db.isOpen())
        m_db.close();
}

void Database::createTables()
{
    QSqlQuery q(m_db);

    q.exec(
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  session_id TEXT PRIMARY KEY,"
        "  title TEXT,"
        "  workspace TEXT,"
        "  mode TEXT,"
        "  created_at INTEGER,"
        "  updated_at INTEGER"
        ")");

    q.exec(
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT REFERENCES sessions(session_id),"
        "  role TEXT,"
        "  content TEXT,"
        "  tool_name TEXT,"
        "  tool_input TEXT,"
        "  turn_id INTEGER,"
        "  timestamp INTEGER"
        ")");

    q.exec(
        "CREATE TABLE IF NOT EXISTS snapshots ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  session_id TEXT REFERENCES sessions(session_id),"
        "  turn_id INTEGER,"
        "  file_path TEXT,"
        "  content BLOB,"
        "  git_stash TEXT,"
        "  timestamp INTEGER"
        ")");
}

void Database::saveSession(const SessionInfo &info)
{
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT OR REPLACE INTO sessions (session_id, title, workspace, mode, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    q.addBindValue(info.sessionId);
    q.addBindValue(info.title);
    q.addBindValue(info.workspace);
    q.addBindValue(info.mode);
    q.addBindValue(info.createdAt);
    q.addBindValue(info.updatedAt);
    q.exec();
}

QList<SessionInfo> Database::loadSessions()
{
    QList<SessionInfo> list;
    QSqlQuery q(m_db);
    q.exec("SELECT session_id, title, workspace, mode, created_at, updated_at "
           "FROM sessions ORDER BY updated_at DESC");
    while (q.next()) {
        SessionInfo info;
        info.sessionId = q.value(0).toString();
        info.title = q.value(1).toString();
        info.workspace = q.value(2).toString();
        info.mode = q.value(3).toString();
        info.createdAt = q.value(4).toLongLong();
        info.updatedAt = q.value(5).toLongLong();
        list.append(info);
    }
    return list;
}

void Database::deleteSession(const QString &sessionId)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM messages WHERE session_id = ?");
    q.addBindValue(sessionId);
    q.exec();

    q.prepare("DELETE FROM snapshots WHERE session_id = ?");
    q.addBindValue(sessionId);
    q.exec();

    q.prepare("DELETE FROM sessions WHERE session_id = ?");
    q.addBindValue(sessionId);
    q.exec();
}

void Database::saveMessage(const MessageRecord &msg)
{
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO messages (session_id, role, content, tool_name, tool_input, turn_id, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(msg.sessionId);
    q.addBindValue(msg.role);
    q.addBindValue(msg.content);
    q.addBindValue(msg.toolName);
    q.addBindValue(msg.toolInput);
    q.addBindValue(msg.turnId);
    q.addBindValue(msg.timestamp);
    q.exec();
}

void Database::updateMessageSessionId(const QString &oldSessionId, const QString &newSessionId)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE messages SET session_id = ? WHERE session_id = ?");
    q.addBindValue(newSessionId);
    q.addBindValue(oldSessionId);
    q.exec();

    // Also update the sessions table
    q.prepare("UPDATE sessions SET session_id = ? WHERE session_id = ?");
    q.addBindValue(newSessionId);
    q.addBindValue(oldSessionId);
    q.exec();
}

QList<MessageRecord> Database::loadMessages(const QString &sessionId)
{
    QList<MessageRecord> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, session_id, role, content, tool_name, tool_input, turn_id, timestamp "
              "FROM messages WHERE session_id = ? ORDER BY id ASC");
    q.addBindValue(sessionId);
    q.exec();
    while (q.next()) {
        MessageRecord msg;
        msg.id = q.value(0).toInt();
        msg.sessionId = q.value(1).toString();
        msg.role = q.value(2).toString();
        msg.content = q.value(3).toString();
        msg.toolName = q.value(4).toString();
        msg.toolInput = q.value(5).toString();
        msg.turnId = q.value(6).toInt();
        msg.timestamp = q.value(7).toLongLong();
        list.append(msg);
    }
    return list;
}

void Database::saveSnapshot(const SnapshotRecord &snap)
{
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO snapshots (session_id, turn_id, file_path, content, git_stash, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    q.addBindValue(snap.sessionId);
    q.addBindValue(snap.turnId);
    q.addBindValue(snap.filePath);
    q.addBindValue(snap.content);
    q.addBindValue(snap.gitStash);
    q.addBindValue(snap.timestamp);
    q.exec();
}

QList<SnapshotRecord> Database::loadSnapshots(const QString &sessionId, int turnId)
{
    QList<SnapshotRecord> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, session_id, turn_id, file_path, content, git_stash, timestamp "
              "FROM snapshots WHERE session_id = ? AND turn_id = ?");
    q.addBindValue(sessionId);
    q.addBindValue(turnId);
    q.exec();
    while (q.next()) {
        SnapshotRecord snap;
        snap.id = q.value(0).toInt();
        snap.sessionId = q.value(1).toString();
        snap.turnId = q.value(2).toInt();
        snap.filePath = q.value(3).toString();
        snap.content = q.value(4).toByteArray();
        snap.gitStash = q.value(5).toString();
        snap.timestamp = q.value(6).toLongLong();
        list.append(snap);
    }
    return list;
}

void Database::deleteSnapshots(const QString &sessionId, int turnId)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM snapshots WHERE session_id = ? AND turn_id = ?");
    q.addBindValue(sessionId);
    q.addBindValue(turnId);
    q.exec();
}
