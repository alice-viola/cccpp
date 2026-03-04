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
        "CREATE TABLE IF NOT EXISTS checkpoints ("
        "  session_id TEXT NOT NULL REFERENCES sessions(session_id),"
        "  turn_id INTEGER NOT NULL,"
        "  uuid TEXT NOT NULL,"
        "  timestamp INTEGER,"
        "  PRIMARY KEY (session_id, turn_id)"
        ")");

    // Drop legacy snapshots table if it exists (replaced by CLI checkpointing)
    q.exec("DROP TABLE IF EXISTS snapshots");

    // Migrations — add columns introduced after initial schema
    q.exec("ALTER TABLE sessions ADD COLUMN favorite INTEGER DEFAULT 0");
}

void Database::saveSession(const SessionInfo &info)
{
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT OR REPLACE INTO sessions (session_id, title, workspace, mode, created_at, updated_at, favorite) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(info.sessionId);
    q.addBindValue(info.title);
    q.addBindValue(info.workspace);
    q.addBindValue(info.mode);
    q.addBindValue(info.createdAt);
    q.addBindValue(info.updatedAt);
    q.addBindValue(info.favorite ? 1 : 0);
    q.exec();
}

QList<SessionInfo> Database::loadSessions()
{
    QList<SessionInfo> list;
    QSqlQuery q(m_db);
    q.exec("SELECT session_id, title, workspace, mode, created_at, updated_at, favorite "
           "FROM sessions ORDER BY updated_at DESC");
    while (q.next()) {
        SessionInfo info;
        info.sessionId = q.value(0).toString();
        info.title = q.value(1).toString();
        info.workspace = q.value(2).toString();
        info.mode = q.value(3).toString();
        info.createdAt = q.value(4).toLongLong();
        info.updatedAt = q.value(5).toLongLong();
        info.favorite = q.value(6).toInt() != 0;
        list.append(info);
    }
    return list;
}

SessionInfo Database::loadSession(const QString &sessionId)
{
    SessionInfo info;
    QSqlQuery q(m_db);
    q.prepare("SELECT session_id, title, workspace, mode, created_at, updated_at, favorite "
              "FROM sessions WHERE session_id = ? LIMIT 1");
    q.addBindValue(sessionId);
    q.exec();
    if (q.next()) {
        info.sessionId = q.value(0).toString();
        info.title = q.value(1).toString();
        info.workspace = q.value(2).toString();
        info.mode = q.value(3).toString();
        info.createdAt = q.value(4).toLongLong();
        info.updatedAt = q.value(5).toLongLong();
        info.favorite = q.value(6).toInt() != 0;
    }
    return info;
}

void Database::deleteSession(const QString &sessionId)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM messages WHERE session_id = ?");
    q.addBindValue(sessionId);
    q.exec();

    q.prepare("DELETE FROM checkpoints WHERE session_id = ?");
    q.addBindValue(sessionId);
    q.exec();

    q.prepare("DELETE FROM sessions WHERE session_id = ?");
    q.addBindValue(sessionId);
    q.exec();
}

void Database::deleteStalePendingSessions()
{
    QSqlQuery q(m_db);
    q.exec("DELETE FROM messages WHERE session_id LIKE 'pending-%'");
    q.exec("DELETE FROM checkpoints WHERE session_id LIKE 'pending-%'");
    q.exec("DELETE FROM sessions WHERE session_id LIKE 'pending-%'");
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

    q.prepare("UPDATE checkpoints SET session_id = ? WHERE session_id = ?");
    q.addBindValue(newSessionId);
    q.addBindValue(oldSessionId);
    q.exec();

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

int Database::turnCountForSession(const QString &sessionId)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COALESCE(MAX(turn_id), 0) FROM messages WHERE session_id = ?");
    q.addBindValue(sessionId);
    q.exec();
    if (q.next())
        return q.value(0).toInt();
    return 0;
}

QMap<QString, int> Database::turnCountsForSessions(const QStringList &sessionIds)
{
    QMap<QString, int> result;
    if (sessionIds.isEmpty()) return result;

    QStringList placeholders;
    for (int i = 0; i < sessionIds.size(); ++i)
        placeholders << "?";

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT session_id, COALESCE(MAX(turn_id), 0) "
        "FROM messages WHERE session_id IN (%1) GROUP BY session_id")
        .arg(placeholders.join(",")));

    for (const auto &sid : sessionIds)
        q.addBindValue(sid);
    q.exec();

    while (q.next())
        result[q.value(0).toString()] = q.value(1).toInt();

    return result;
}

void Database::saveCheckpoint(const CheckpointRecord &cp)
{
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT OR REPLACE INTO checkpoints (session_id, turn_id, uuid, timestamp) "
        "VALUES (?, ?, ?, ?)");
    q.addBindValue(cp.sessionId);
    q.addBindValue(cp.turnId);
    q.addBindValue(cp.uuid);
    q.addBindValue(cp.timestamp);
    q.exec();
}

QList<CheckpointRecord> Database::loadCheckpoints(const QString &sessionId)
{
    QList<CheckpointRecord> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT session_id, turn_id, uuid, timestamp "
              "FROM checkpoints WHERE session_id = ? ORDER BY turn_id ASC");
    q.addBindValue(sessionId);
    q.exec();
    while (q.next()) {
        CheckpointRecord cp;
        cp.sessionId = q.value(0).toString();
        cp.turnId = q.value(1).toInt();
        cp.uuid = q.value(2).toString();
        cp.timestamp = q.value(3).toLongLong();
        list.append(cp);
    }
    return list;
}

QString Database::checkpointUuid(const QString &sessionId, int turnId)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT uuid FROM checkpoints WHERE session_id = ? AND turn_id = ?");
    q.addBindValue(sessionId);
    q.addBindValue(turnId);
    q.exec();
    if (q.next())
        return q.value(0).toString();
    return {};
}
