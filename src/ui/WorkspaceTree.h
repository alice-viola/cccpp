#pragma once

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QStyledItemDelegate>
#include <QLabel>
#include <QMap>
#include "core/GitManager.h"

enum class FileChangeType { Modified, Created, Deleted };

class ChangedFileDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setChangedFiles(const QMap<QString, FileChangeType> *files) { m_files = files; }
    void setGitStatus(const QMap<QString, GitFileStatus> *gitStatus) { m_gitStatus = gitStatus; }
    void setModel(QFileSystemModel *model) { m_model = model; }
    void setRootPath(const QString &path) { m_rootPath = path; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    const QMap<QString, FileChangeType> *m_files = nullptr;
    const QMap<QString, GitFileStatus> *m_gitStatus = nullptr;
    QFileSystemModel *m_model = nullptr;
    QString m_rootPath;
};

class WorkspaceTree : public QWidget {
    Q_OBJECT
public:
    explicit WorkspaceTree(QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    void markFileChanged(const QString &filePath, FileChangeType type = FileChangeType::Modified);
    void clearChangeMarkers();

    void setGitFileEntries(const QList<GitFileEntry> &entries);
    void clearGitStatus();

signals:
    void fileSelected(const QString &filePath);
    void fileCreated(const QString &filePath);
    void fileDeleted(const QString &filePath);
    void folderCreated(const QString &folderPath);
    void folderDeleted(const QString &folderPath);

private slots:
    void onContextMenu(const QPoint &pos);

private:
    void applyThemeColors();
    void createNewFile(const QString &parentDir);
    void createNewFolder(const QString &parentDir);
    void deleteSelected(const QString &path, bool isDir);
    QString contextDirectory(const QModelIndex &index) const;

    QLabel *m_header = nullptr;
    QTreeView *m_tree;
    QFileSystemModel *m_model;
    ChangedFileDelegate *m_delegate;
    QMap<QString, FileChangeType> m_changedFiles;
    QMap<QString, GitFileStatus> m_gitStatus;
    QString m_rootPath;
};
