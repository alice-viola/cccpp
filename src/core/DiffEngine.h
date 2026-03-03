#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>

struct DiffHunk {
    enum Type { Added, Removed, Context };
    Type type;
    int startLine;
    int count;
    QStringList lines;
};

struct FileDiff {
    QString filePath;
    QList<DiffHunk> hunks;
    bool isNewFile = false;
    bool isDeleted = false;
};

class DiffEngine : public QObject {
    Q_OBJECT
public:
    explicit DiffEngine(QObject *parent = nullptr);

    FileDiff computeDiff(const QString &oldContent, const QString &newContent,
                         const QString &filePath = {});

    void setCurrentSessionId(const QString &sessionId);

    void recordEditToolChange(const QString &filePath,
                              const QString &oldString, const QString &newString);

    void recordWriteToolChange(const QString &filePath, const QString &newContent);

    QList<FileDiff> pendingDiffs() const { return m_pendingDiffs; }
    FileDiff diffForFile(const QString &filePath) const;
    QStringList changedFiles() const { return m_fileDiffs.keys(); }
    QStringList changedFilesForSession(const QString &sessionId) const;
    void clearPendingDiffs();

    // Per-file line counts for effects panel
    int linesAddedForFile(const QString &filePath) const;
    int linesRemovedForFile(const QString &filePath) const;

signals:
    void fileChanged(const QString &filePath, const FileDiff &diff);
    void sessionFileChanged(const QString &sessionId, const QString &filePath);

private:
    struct EditRecord {
        int line;
        QString oldText;
        QString newText;
    };

    QList<int> computeLCS(const QStringList &a, const QStringList &b);

    QList<FileDiff> m_pendingDiffs;
    QMap<QString, FileDiff> m_fileDiffs;
    QMap<QString, QString> m_originalContents;
    QString m_currentSessionId;
    // sessionId -> set of file paths changed
    QMap<QString, QStringList> m_sessionFiles;
};
