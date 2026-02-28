#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QProcess>

class Database;

class SnapshotManager : public QObject {
    Q_OBJECT
public:
    explicit SnapshotManager(QObject *parent = nullptr);

    void setWorkingDirectory(const QString &dir);
    void setDatabase(Database *db);
    void setSessionId(const QString &id);

    void beginTurn(int turnId);
    void recordEditOldString(const QString &filePath, const QString &oldString);
    void commitTurn();
    bool revertTurn(int turnId);

    int currentTurnId() const { return m_currentTurnId; }
    bool isGitRepo() const { return m_isGitRepo; }

signals:
    void revertCompleted(int turnId);
    void revertFailed(int turnId, const QString &error);

private:
    QString runGit(const QStringList &args);
    bool detectGitRepo();

    QString m_workingDir;
    Database *m_db = nullptr;
    QString m_sessionId;
    int m_currentTurnId = 0;
    bool m_isGitRepo = false;
    QString m_currentStashHash;
    QMap<QString, QString> m_editOldStrings; // filePath -> old content accumulated
};
