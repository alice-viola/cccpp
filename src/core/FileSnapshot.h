#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QByteArray>

class FileSnapshot : public QObject {
    Q_OBJECT
public:
    explicit FileSnapshot(QObject *parent = nullptr);

    void captureFile(const QString &filePath);
    void captureDirectory(const QString &dirPath, const QStringList &extensions = {});
    QByteArray originalContent(const QString &filePath) const;
    bool hasSnapshot(const QString &filePath) const;
    QStringList snapshotFiles() const;
    void clear();

private:
    QMap<QString, QByteArray> m_snapshots;
};
