#pragma once

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QStyledItemDelegate>
#include <QMap>

enum class FileChangeType { Modified, Created, Deleted };

class ChangedFileDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setChangedFiles(const QMap<QString, FileChangeType> *files) { m_files = files; }
    void setModel(QFileSystemModel *model) { m_model = model; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    const QMap<QString, FileChangeType> *m_files = nullptr;
    QFileSystemModel *m_model = nullptr;
};

class WorkspaceTree : public QWidget {
    Q_OBJECT
public:
    explicit WorkspaceTree(QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    void markFileChanged(const QString &filePath, FileChangeType type = FileChangeType::Modified);
    void clearChangeMarkers();

signals:
    void fileSelected(const QString &filePath);

private:
    QTreeView *m_tree;
    QFileSystemModel *m_model;
    ChangedFileDelegate *m_delegate;
    QMap<QString, FileChangeType> m_changedFiles;
};
