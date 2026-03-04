#include "core/SessionManager.h"
#include <QDateTime>
#include <QUuid>

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
{
}

QString SessionManager::createSession(const QString &workspace, const QString &mode)
{
    QString tempId = QUuid::createUuid().toString(QUuid::WithoutBraces);

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

    // Update parentSessionId references in child sessions
    for (auto &s : m_sessions) {
        if (s.parentSessionId == tempId)
            s.parentSessionId = realId;
    }

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

void SessionManager::setSessionFavorite(const QString &sessionId, bool favorite)
{
    if (m_sessions.contains(sessionId)) {
        m_sessions[sessionId].favorite = favorite;
        emit sessionUpdated(sessionId);
    }
}

void SessionManager::removeSession(const QString &sessionId)
{
    if (m_sessions.remove(sessionId) > 0)
        emit sessionDeleted(sessionId);
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

QString SessionManager::createChildSession(const QString &parentId, const QString &workspace,
                                            const QString &mode, const QString &task)
{
    QString childId = createSession(workspace, mode);
    if (m_sessions.contains(childId)) {
        auto &info = m_sessions[childId];
        info.parentSessionId = parentId;
        info.delegationTask = task;
        info.delegationStatus = SessionInfo::Pending;
        info.title = task.left(30) + (task.length() > 30 ? QStringLiteral("\u2026") : QString());
    }
    return childId;
}

QList<SessionInfo> SessionManager::childSessions(const QString &parentId) const
{
    QList<SessionInfo> children;
    for (const auto &s : m_sessions) {
        if (s.parentSessionId == parentId)
            children.append(s);
    }
    return children;
}

void SessionManager::setDelegationStatus(const QString &sessionId, SessionInfo::DelegationStatus status)
{
    if (m_sessions.contains(sessionId)) {
        m_sessions[sessionId].delegationStatus = status;
        m_sessions[sessionId].updatedAt = QDateTime::currentSecsSinceEpoch();
        emit sessionUpdated(sessionId);
    }
}

void SessionManager::setDelegationResult(const QString &sessionId, const QString &result)
{
    if (m_sessions.contains(sessionId)) {
        m_sessions[sessionId].delegationResult = result;
        emit sessionUpdated(sessionId);
    }
}
