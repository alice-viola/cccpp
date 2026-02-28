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

    void recordEditToolChange(const QString &filePath,
                              const QString &oldString, const QString &newString);

    void recordWriteToolChange(const QString &filePath, const QString &newContent);

    QList<FileDiff> pendingDiffs() const { return m_pendingDiffs; }
    FileDiff diffForFile(const QString &filePath) const;
    QStringList changedFiles() const { return m_fileDiffs.keys(); }
    void clearPendingDiffs();

signals:
    void fileChanged(const QString &filePath, const FileDiff &diff);

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
};
