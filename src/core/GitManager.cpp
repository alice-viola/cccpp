#include "core/GitManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>

GitManager::GitManager(QObject *parent)
    : QObject(parent)
{
    resolveGitBinary();

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(500);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        m_refreshScheduled = false;
        enqueueOp([this] { doRefreshStatus(); });
    });
}

GitManager::~GitManager()
{
    stopWatching();
}

// ---------------------------------------------------------------------------
// Git binary resolution
// ---------------------------------------------------------------------------

void GitManager::resolveGitBinary()
{
    QString found = QStandardPaths::findExecutable("git");
    if (!found.isEmpty()) {
        m_gitBinary = found;
        return;
    }

    const QStringList candidates = {
        "/usr/bin/git",
        "/usr/local/bin/git",
        "/opt/homebrew/bin/git",
    };
    for (const auto &path : candidates) {
        if (QFile::exists(path)) {
            m_gitBinary = path;
            return;
        }
    }

    m_gitBinary = "git";
}

// ---------------------------------------------------------------------------
// Working directory & repo detection
// ---------------------------------------------------------------------------

void GitManager::setWorkingDirectory(const QString &dir)
{
    stopWatching();
    m_workingDir = dir;
    m_entries.clear();
    m_currentBranch.clear();
    detectRepo();

    if (m_isGitRepo) {
        startWatching();
        refreshStatus();
    }
}

void GitManager::detectRepo()
{
    m_isGitRepo = false;
    if (m_workingDir.isEmpty())
        return;

    QDir d(m_workingDir);
    while (true) {
        if (QDir(d.filePath(".git")).exists()) {
            m_isGitRepo = true;
            return;
        }
        if (!d.cdUp())
            break;
    }
}

// ---------------------------------------------------------------------------
// File system watcher for auto-refresh
// ---------------------------------------------------------------------------

void GitManager::startWatching()
{
    if (m_watcher) return;
    m_watcher = new QFileSystemWatcher(this);

    QString gitIndex = m_workingDir + "/.git/index";
    if (QFile::exists(gitIndex))
        m_watcher->addPath(gitIndex);

    QString gitHead = m_workingDir + "/.git/HEAD";
    if (QFile::exists(gitHead))
        m_watcher->addPath(gitHead);

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // Re-add the path (some platforms drop watches after change)
        if (QFile::exists(path))
            m_watcher->addPath(path);
        scheduleRefresh();
    });
}

void GitManager::stopWatching()
{
    if (m_watcher) {
        delete m_watcher;
        m_watcher = nullptr;
    }
    m_debounce->stop();
    m_refreshScheduled = false;
}

void GitManager::scheduleRefresh()
{
    if (!m_refreshScheduled) {
        m_refreshScheduled = true;
        m_debounce->start();
    }
}

// ---------------------------------------------------------------------------
// Operation queue (serializes git calls)
// ---------------------------------------------------------------------------

void GitManager::enqueueOp(std::function<void()> op)
{
    m_opQueue.enqueue(std::move(op));
    if (!m_opRunning)
        drainQueue();
}

void GitManager::drainQueue()
{
    if (m_opQueue.isEmpty()) {
        m_opRunning = false;
        return;
    }
    m_opRunning = true;
    auto op = m_opQueue.dequeue();
    op();
}

// ---------------------------------------------------------------------------
// Async git runner
// ---------------------------------------------------------------------------

void GitManager::runAsync(const QStringList &args,
                          std::function<void(int, const QString &, const QString &)> callback)
{
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(m_workingDir);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, callback](int exitCode, QProcess::ExitStatus) {
        QString out = QString::fromUtf8(proc->readAllStandardOutput());
        QString err = QString::fromUtf8(proc->readAllStandardError());
        proc->deleteLater();
        if (callback)
            callback(exitCode, out, err);
        drainQueue();
    });

    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, callback, args](QProcess::ProcessError error) {
        Q_UNUSED(error);
        QString errMsg = proc->errorString();
        proc->deleteLater();
        if (callback)
            callback(-1, {}, errMsg);
        drainQueue();
    });

    proc->start(m_gitBinary, args);
}

QString GitManager::runGitSync(const QStringList &args, int timeoutMs)
{
    QProcess proc;
    proc.setWorkingDirectory(m_workingDir);
    proc.start(m_gitBinary, args);
    proc.waitForFinished(timeoutMs);
    return QString::fromUtf8(proc.readAllStandardOutput()) +
           QString::fromUtf8(proc.readAllStandardError());
}

// ---------------------------------------------------------------------------
// Refresh status
// ---------------------------------------------------------------------------

void GitManager::refreshStatus()
{
    if (!m_isGitRepo) return;
    enqueueOp([this] { doRefreshStatus(); });
}

void GitManager::doRefreshStatus()
{
    // Fetch branch + status in one shot
    runAsync({"status", "--porcelain=v1", "--branch", "-uall", "--ignore-submodules"}, [this](int exitCode, const QString &out, const QString &err) {
        if (exitCode != 0) {
            emit errorOccurred("status", err);
            return;
        }
        parseStatusOutput(out);
    });
}

void GitManager::parseStatusOutput(const QString &output)
{
    m_entries.clear();
    QString oldBranch = m_currentBranch;

    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith("## ")) {
            // Parse branch: "## main...origin/main" or "## HEAD (no branch)"
            QString branchPart = line.mid(3);
            int dotdot = branchPart.indexOf("...");
            if (dotdot >= 0)
                m_currentBranch = branchPart.left(dotdot);
            else if (branchPart.contains("(no branch)") || branchPart.contains("HEAD"))
                m_currentBranch = "(detached)";
            else
                m_currentBranch = branchPart.trimmed();
            continue;
        }

        if (line.length() < 4) continue;

        QChar x = line[0]; // index status
        QChar y = line[1]; // worktree status

        GitFileEntry entry;
        entry.indexStatus = charToStatus(x);
        entry.workStatus  = charToStatus(y);

        // Handle renames: "R  old -> new"
        QString pathPart = line.mid(3);
        if (x == 'R' || x == 'C') {
            int arrow = pathPart.indexOf(" -> ");
            if (arrow >= 0) {
                entry.oldPath  = pathPart.left(arrow);
                entry.filePath = pathPart.mid(arrow + 4);
            } else {
                entry.filePath = pathPart;
            }
        } else {
            entry.filePath = pathPart;
        }

        // Conflict detection: both modified, or various unmerged combos
        if ((x == 'U') || (y == 'U') ||
            (x == 'A' && y == 'A') ||
            (x == 'D' && y == 'D')) {
            entry.indexStatus = GitFileStatus::Conflicted;
            entry.workStatus  = GitFileStatus::Conflicted;
        }

        m_entries.append(entry);
    }

    if (m_currentBranch != oldBranch)
        emit branchChanged(m_currentBranch);

    emit statusChanged(m_entries);
}

GitFileStatus GitManager::charToStatus(QChar c) const
{
    switch (c.toLatin1()) {
    case 'M': return GitFileStatus::Modified;
    case 'A': return GitFileStatus::Added;
    case 'D': return GitFileStatus::Deleted;
    case 'R': return GitFileStatus::Renamed;
    case 'C': return GitFileStatus::Copied;
    case '?': return GitFileStatus::Untracked;
    case '!': return GitFileStatus::Ignored;
    case 'U': return GitFileStatus::Conflicted;
    case ' ': return GitFileStatus::Unmodified;
    default:  return GitFileStatus::Unmodified;
    }
}

// ---------------------------------------------------------------------------
// File diff
// ---------------------------------------------------------------------------

void GitManager::requestFileDiff(const QString &filePath, bool staged)
{
    if (!m_isGitRepo) return;
    enqueueOp([this, filePath, staged] { doRequestFileDiff(filePath, staged); });
}

void GitManager::doRequestFileDiff(const QString &filePath, bool staged)
{
    // First check if it's binary
    QStringList numstatArgs = {"diff", "--numstat", "--ignore-submodules"};
    if (staged) numstatArgs << "--cached";
    numstatArgs << "--" << filePath;

    runAsync(numstatArgs, [this, filePath, staged](int, const QString &numOut, const QString &) {
        bool isBinary = numOut.startsWith("-\t-\t");

        if (isBinary) {
            GitUnifiedDiff diff;
            diff.filePath = filePath;
            diff.isBinary = true;
            emit fileDiffReady(filePath, staged, diff);
            return;
        }

        // Get old content (HEAD or staged version)
        QString blobSpec = staged ? QStringLiteral(":0:%1").arg(filePath)
                                  : QStringLiteral("HEAD:%1").arg(filePath);
        QStringList showArgs = {"show", blobSpec};

        runAsync(showArgs, [this, filePath, staged](int exitCode, const QString &oldContent, const QString &) {
            QString old = (exitCode == 0) ? oldContent : QString();

            // Get new content
            if (staged) {
                // staged diff: new = index version
                QStringList indexArgs = {"show", QStringLiteral(":0:%1").arg(filePath)};
                // Actually for staged diff, old=HEAD new=index
                // old is HEAD:<file>, new is :0:<file>
                // We already got old from HEAD, now get index
                QStringList showIndexArgs = {"show", QStringLiteral(":0:%1").arg(filePath)};
                // Wait — for staged: old=HEAD new=index
                // We ran `git show HEAD:<file>` to get old, now get index version
                // Actually `git show :0:<file>` IS the index version. For staged diff,
                // old = HEAD version, new = index version.
                // We already fetched old = HEAD (exitCode == 0 means it exists)
                // Now fetch new = index
                runAsync({"show", QStringLiteral(":0:%1").arg(filePath)},
                         [this, filePath, staged, old](int ec2, const QString &newContent, const QString &) {
                    GitUnifiedDiff diff;
                    diff.filePath = filePath;
                    diff.oldContent = old;
                    diff.newContent = (ec2 == 0) ? newContent : QString();
                    emit fileDiffReady(filePath, staged, diff);
                });
            } else {
                // unstaged diff: old = HEAD (or index), new = working tree
                QString fullPath = m_workingDir + "/" + filePath;
                QFile f(fullPath);
                QString newContent;
                if (f.open(QIODevice::ReadOnly))
                    newContent = QString::fromUtf8(f.readAll());

                GitUnifiedDiff diff;
                diff.filePath = filePath;
                diff.oldContent = old;
                diff.newContent = newContent;
                emit fileDiffReady(filePath, staged, diff);
            }
        });
    });
}

// ---------------------------------------------------------------------------
// Staging / Unstaging
// ---------------------------------------------------------------------------

void GitManager::stageFile(const QString &filePath)
{
    stageFiles({filePath});
}

void GitManager::stageFiles(const QStringList &paths)
{
    if (!m_isGitRepo || paths.isEmpty()) return;
    enqueueOp([this, paths] { doStageFiles(paths); });
}

void GitManager::stageAll()
{
    if (!m_isGitRepo) return;
    enqueueOp([this] { doStageFiles({"."}); });
}

void GitManager::doStageFiles(const QStringList &paths)
{
    QStringList args = {"add", "--"};
    args.append(paths);
    runAsync(args, [this](int exitCode, const QString &, const QString &err) {
        if (exitCode != 0)
            emit errorOccurred("stage", err);
        scheduleRefresh();
    });
}

void GitManager::unstageFile(const QString &filePath)
{
    unstageFiles({filePath});
}

void GitManager::unstageFiles(const QStringList &paths)
{
    if (!m_isGitRepo || paths.isEmpty()) return;
    enqueueOp([this, paths] { doUnstageFiles(paths); });
}

void GitManager::unstageAll()
{
    if (!m_isGitRepo) return;
    enqueueOp([this] { doUnstageFiles({"."}); });
}

void GitManager::doUnstageFiles(const QStringList &paths)
{
    QStringList args = {"restore", "--staged", "--"};
    args.append(paths);
    runAsync(args, [this](int exitCode, const QString &, const QString &err) {
        if (exitCode != 0)
            emit errorOccurred("unstage", err);
        scheduleRefresh();
    });
}

// ---------------------------------------------------------------------------
// Discard
// ---------------------------------------------------------------------------

void GitManager::discardFile(const QString &filePath)
{
    if (!m_isGitRepo) return;
    enqueueOp([this, filePath] { doDiscardFile(filePath); });
}

void GitManager::doDiscardFile(const QString &filePath)
{
    // For untracked files, just remove them
    bool isUntracked = false;
    for (const auto &e : qAsConst(m_entries)) {
        if (e.filePath == filePath && e.workStatus == GitFileStatus::Untracked) {
            isUntracked = true;
            break;
        }
    }

    if (isUntracked) {
        QFile::remove(m_workingDir + "/" + filePath);
        scheduleRefresh();
        return;
    }

    QStringList args = {"checkout", "--", filePath};
    runAsync(args, [this](int exitCode, const QString &, const QString &err) {
        if (exitCode != 0)
            emit errorOccurred("discard", err);
        scheduleRefresh();
    });
}

void GitManager::discardAll()
{
    if (!m_isGitRepo) return;
    enqueueOp([this] { doDiscardAll(); });
}

void GitManager::doDiscardAll()
{
    runAsync({"checkout", "--", "."}, [this](int exitCode, const QString &, const QString &err) {
        if (exitCode != 0)
            emit errorOccurred("discard all", err);
        // Also clean untracked files
        runAsync({"clean", "-fd"}, [this](int exitCode2, const QString &, const QString &err2) {
            if (exitCode2 != 0)
                emit errorOccurred("clean", err2);
            scheduleRefresh();
        });
    });
}

// ---------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------

void GitManager::commit(const QString &message)
{
    if (!m_isGitRepo) return;
    enqueueOp([this, message] { doCommit(message); });
}

void GitManager::doCommit(const QString &message)
{
    runAsync({"commit", "-m", message}, [this, message](int exitCode, const QString &out, const QString &err) {
        if (exitCode != 0) {
            emit commitFailed(err.isEmpty() ? out : err);
            return;
        }

        // Extract commit hash from output (typically: "[branch abc1234] message")
        QString hash;
        int bracket = out.indexOf('[');
        int space = out.indexOf(' ', bracket + 1);
        int close = out.indexOf(']', space);
        if (bracket >= 0 && space > bracket && close > space)
            hash = out.mid(space + 1, close - space - 1);

        emit commitSucceeded(hash, message);
        scheduleRefresh();
    });
}
