#include "core/SessionManager.h"
#include <QDateTime>
#include <QUuid>

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
{
}

QString SessionManager::createSession(const QString &workspace, const QString &mode)
{
    // Temporary ID until Claude Code returns the real session_id
    QString tempId = QStringLiteral("pending-%1").arg(m_nextTempId++);

    SessionInfo info;
    info.sessionId = tempId;
    info.title = QStringLiteral("Chat %1").arg(m_sessions.size() + 1);
    info.workspace = workspace;
    info.mode = mode;
    info.createdAt = QDateTime::currentSecsSinceEpoch();
    info.updatedAt = info.createdAt;

    m_sessions.insert(tempId, info);
    emit sessionCreated(tempId);
    return tempId;
}

void SessionManager::registerSession(const QString &sessionId, const SessionInfo &info)
{
    m_sessions.insert(sessionId, info);
    emit sessionCreated(sessionId);
}

void SessionManager::updateSessionId(const QString &tempId, const QString &realId)
{
    if (!m_sessions.contains(tempId))
        return;

    SessionInfo info = m_sessions.take(tempId);
    info.sessionId = realId;
    info.updatedAt = QDateTime::currentSecsSinceEpoch();
    m_sessions.insert(realId, info);
    emit sessionUpdated(realId);
}

void SessionManager::setSessionTitle(const QString &sessionId, const QString &title)
{
    if (m_sessions.contains(sessionId)) {
        m_sessions[sessionId].title = title;
        m_sessions[sessionId].updatedAt = QDateTime::currentSecsSinceEpoch();
        emit sessionUpdated(sessionId);
    }
}

SessionInfo SessionManager::sessionInfo(const QString &sessionId) const
{
    return m_sessions.value(sessionId);
}

QList<SessionInfo> SessionManager::allSessions() const
{
    return m_sessions.values();
}

bool SessionManager::hasSession(const QString &sessionId) const
{
    return m_sessions.contains(sessionId);
}
