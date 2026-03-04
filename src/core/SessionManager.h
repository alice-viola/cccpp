#pragma once

#include <QObject>
#include <QString>
#include <QMap>

struct SessionInfo {
    QString sessionId;
    QString title;
    QString workspace;
    QString mode = "agent";
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    bool favorite = false;

    // Delegation hierarchy
    QString parentSessionId;
    QString pipelineId;
    QString pipelineNodeId;
    QString delegationTask;
    enum DelegationStatus { None, Pending, Running, Completed, Failed };
    DelegationStatus delegationStatus = None;
    QString delegationResult;
};

class SessionManager : public QObject {
    Q_OBJECT
public:
    explicit SessionManager(QObject *parent = nullptr);

    QString createSession(const QString &workspace, const QString &mode = "agent");
    void registerSession(const QString &sessionId, const SessionInfo &info);
    void updateSessionId(const QString &tempId, const QString &realId);
    void setSessionTitle(const QString &sessionId, const QString &title);
    void setSessionFavorite(const QString &sessionId, bool favorite);
    void removeSession(const QString &sessionId);

    SessionInfo sessionInfo(const QString &sessionId) const;
    QList<SessionInfo> allSessions() const;
    bool hasSession(const QString &sessionId) const;

    // Delegation hierarchy
    QString createChildSession(const QString &parentId, const QString &workspace,
                               const QString &mode, const QString &task);
    QList<SessionInfo> childSessions(const QString &parentId) const;
    void setDelegationStatus(const QString &sessionId, SessionInfo::DelegationStatus status);
    void setDelegationResult(const QString &sessionId, const QString &result);

signals:
    void sessionCreated(const QString &sessionId);
    void sessionUpdated(const QString &sessionId);
    void sessionDeleted(const QString &sessionId);

private:
    QMap<QString, SessionInfo> m_sessions;
};
