#include "core/FileSnapshot.h"
#include <QFile>
#include <QDir>
#include <QDirIterator>

FileSnapshot::FileSnapshot(QObject *parent)
    : QObject(parent)
{
}

void FileSnapshot::captureFile(const QString &filePath)
{
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly))
        m_snapshots.insert(filePath, file.readAll());
}

void FileSnapshot::captureDirectory(const QString &dirPath, const QStringList &extensions)
{
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        if (!extensions.isEmpty()) {
            bool match = false;
            for (const QString &ext : extensions) {
                if (path.endsWith(ext)) { match = true; break; }
            }
            if (!match) continue;
        }
        captureFile(path);
    }
}

QByteArray FileSnapshot::originalContent(const QString &filePath) const
{
    return m_snapshots.value(filePath);
}

bool FileSnapshot::hasSnapshot(const QString &filePath) const
{
    return m_snapshots.contains(filePath);
}

QStringList FileSnapshot::snapshotFiles() const
{
    return m_snapshots.keys();
}

void FileSnapshot::clear()
{
    m_snapshots.clear();
}
