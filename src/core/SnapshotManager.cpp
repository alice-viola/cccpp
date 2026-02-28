#include "core/SnapshotManager.h"
#include "core/Database.h"
#include "core/GitManager.h"
#include <QDir>
#include <QFile>
#include <QDateTime>

SnapshotManager::SnapshotManager(QObject *parent)
    : QObject(parent)
{
}

void SnapshotManager::setWorkingDirectory(const QString &dir)
{
    m_workingDir = dir;
    if (m_gitManager)
        m_isGitRepo = m_gitManager->isGitRepo();
    else
        m_isGitRepo = QDir(m_workingDir + "/.git").exists();
}

void SnapshotManager::setDatabase(Database *db)
{
    m_db = db;
}

void SnapshotManager::setSessionId(const QString &id)
{
    m_sessionId = id;
}

void SnapshotManager::setGitManager(GitManager *mgr)
{
    m_gitManager = mgr;
}

void SnapshotManager::beginTurn(int turnId)
{
    m_currentTurnId = turnId;
    m_editOldStrings.clear();
    m_currentStashHash.clear();

    if (m_isGitRepo && m_gitManager) {
        m_currentStashHash = m_gitManager->runGitSync({"stash", "create"}).trimmed();
        if (m_currentStashHash.isEmpty())
            m_currentStashHash = m_gitManager->runGitSync({"rev-parse", "HEAD"}).trimmed();
    }
}

void SnapshotManager::recordEditOldString(const QString &filePath, const QString &oldString)
{
    m_editOldStrings[filePath] += oldString;

    if (m_db) {
        SnapshotRecord snap;
        snap.sessionId = m_sessionId;
        snap.turnId = m_currentTurnId;
        snap.filePath = filePath;
        snap.content = oldString.toUtf8();
        snap.gitStash = m_currentStashHash;
        snap.timestamp = QDateTime::currentSecsSinceEpoch();
        m_db->saveSnapshot(snap);
    }
}

void SnapshotManager::commitTurn()
{
    m_editOldStrings.clear();
}

bool SnapshotManager::revertTurn(int turnId)
{
    if (!m_db)
        return false;

    auto snapshots = m_db->loadSnapshots(m_sessionId, turnId);
    if (snapshots.isEmpty()) {
        emit revertFailed(turnId, "No snapshots found for this turn");
        return false;
    }

    if (m_isGitRepo && m_gitManager && !snapshots.first().gitStash.isEmpty()) {
        QString stash = snapshots.first().gitStash;
        QString result = m_gitManager->runGitSync({"checkout", stash, "--", "."});
        if (result.contains("error")) {
            emit revertFailed(turnId, result);
            return false;
        }
    } else {
        for (const auto &snap : snapshots) {
            QFile file(snap.filePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(snap.content);
                file.close();
            }
        }
    }

    emit revertCompleted(turnId);
    return true;
}
