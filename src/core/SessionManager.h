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
};

class SessionManager : public QObject {
    Q_OBJECT
public:
    explicit SessionManager(QObject *parent = nullptr);

    QString createSession(const QString &workspace, const QString &mode = "agent");
    void registerSession(const QString &sessionId, const SessionInfo &info);
    void updateSessionId(const QString &tempId, const QString &realId);
    void setSessionTitle(const QString &sessionId, const QString &title);

    SessionInfo sessionInfo(const QString &sessionId) const;
    QList<SessionInfo> allSessions() const;
    bool hasSession(const QString &sessionId) const;

signals:
    void sessionCreated(const QString &sessionId);
    void sessionUpdated(const QString &sessionId);

private:
    QMap<QString, SessionInfo> m_sessions;
    int m_nextTempId = 1;
};
