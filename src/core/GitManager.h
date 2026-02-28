#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QQueue>
#include <functional>

enum class GitFileStatus {
    Unmodified, Untracked, Modified, Added, Deleted, Renamed, Copied, Conflicted, Ignored
};

struct GitFileEntry {
    QString filePath;
    QString oldPath;
    GitFileStatus indexStatus = GitFileStatus::Unmodified;
    GitFileStatus workStatus  = GitFileStatus::Unmodified;
};

struct GitUnifiedDiff {
    QString filePath;
    QString oldContent;
    QString newContent;
    bool isBinary = false;
};

class GitManager : public QObject {
    Q_OBJECT
public:
    explicit GitManager(QObject *parent = nullptr);
    ~GitManager() override;

    void setWorkingDirectory(const QString &dir);
    QString workingDirectory() const { return m_workingDir; }

    bool isGitRepo() const { return m_isGitRepo; }
    QString currentBranch() const { return m_currentBranch; }
    QList<GitFileEntry> fileEntries() const { return m_entries; }

    // Trigger a manual status refresh
    void refreshStatus();

    // Diff operations
    void requestFileDiff(const QString &filePath, bool staged = false);

    // Staging
    void stageFile(const QString &filePath);
    void stageFiles(const QStringList &paths);
    void stageAll();
    void unstageFile(const QString &filePath);
    void unstageFiles(const QStringList &paths);
    void unstageAll();

    // Discard working tree changes
    void discardFile(const QString &filePath);
    void discardAll();

    // Commit
    void commit(const QString &message);

    // Low-level git runner (synchronous, used by SnapshotManager)
    QString runGitSync(const QStringList &args, int timeoutMs = 5000);

signals:
    void statusChanged(const QList<GitFileEntry> &entries);
    void branchChanged(const QString &branch);
    void fileDiffReady(const QString &filePath, bool staged, const GitUnifiedDiff &diff);
    void commitSucceeded(const QString &hash, const QString &message);
    void commitFailed(const QString &error);
    void errorOccurred(const QString &operation, const QString &message);
    void operationCompleted();

private:
    void detectRepo();
    void startWatching();
    void stopWatching();
    void scheduleRefresh();

    void enqueueOp(std::function<void()> op);
    void drainQueue();

    void doRefreshStatus();
    void parseStatusOutput(const QString &output);
    GitFileStatus charToStatus(QChar c) const;

    void doRequestFileDiff(const QString &filePath, bool staged);
    void doStageFiles(const QStringList &paths);
    void doUnstageFiles(const QStringList &paths);
    void doDiscardFile(const QString &filePath);
    void doDiscardAll();
    void doCommit(const QString &message);

    void runAsync(const QStringList &args,
                  std::function<void(int exitCode, const QString &out, const QString &err)> callback);

    QString m_workingDir;
    bool m_isGitRepo = false;
    QString m_currentBranch;
    QList<GitFileEntry> m_entries;

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce;
    bool m_refreshScheduled = false;

    bool m_opRunning = false;
    QQueue<std::function<void()>> m_opQueue;

    QString m_gitBinary;
    void resolveGitBinary();
};
