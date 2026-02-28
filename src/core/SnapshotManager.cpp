#include "core/SnapshotManager.h"
#include "core/Database.h"
#include <QProcess>
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
    m_isGitRepo = detectGitRepo();
}

void SnapshotManager::setDatabase(Database *db)
{
    m_db = db;
}

void SnapshotManager::setSessionId(const QString &id)
{
    m_sessionId = id;
}

bool SnapshotManager::detectGitRepo()
{
    return QDir(m_workingDir + "/.git").exists();
}

void SnapshotManager::beginTurn(int turnId)
{
    m_currentTurnId = turnId;
    m_editOldStrings.clear();
    m_currentStashHash.clear();

    if (m_isGitRepo) {
        // Create a stash object BEFORE Claude modifies anything
        m_currentStashHash = runGit({"stash", "create"}).trimmed();
        if (m_currentStashHash.isEmpty()) {
            // No changes to stash — record HEAD instead
            m_currentStashHash = runGit({"rev-parse", "HEAD"}).trimmed();
        }
    }
}

void SnapshotManager::recordEditOldString(const QString &filePath, const QString &oldString)
{
    // Accumulate old_string payloads from Edit tool events
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
    // Turn is finalized, snapshot data is already persisted
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

    if (m_isGitRepo && !snapshots.first().gitStash.isEmpty()) {
        // Full revert via git: restore working tree to pre-turn state
        QString stash = snapshots.first().gitStash;
        QString result = runGit({"checkout", stash, "--", "."});
        if (result.contains("error")) {
            emit revertFailed(turnId, result);
            return false;
        }
    } else {
        // Non-git: restore files from stored content
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

QString SnapshotManager::runGit(const QStringList &args)
{
    QProcess proc;
    proc.setWorkingDirectory(m_workingDir);
    proc.start("git", args);
    proc.waitForFinished(5000);
    return QString::fromUtf8(proc.readAllStandardOutput()) +
           QString::fromUtf8(proc.readAllStandardError());
}
