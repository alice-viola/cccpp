#include "ui/WorkspaceTree.h"
#include "ui/FileIconProvider.h"
#include "ui/ThemeManager.h"
#include "ui/ToastManager.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QDir>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QRegularExpression>

class FileFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        auto *fsm = qobject_cast<QFileSystemModel *>(sourceModel());
        if (!fsm) return true;
        QString name = fsm->fileName(fsm->index(row, 0, parent));
        return name != ".DS_Store";
    }
};

static QChar gitStatusLetter(GitFileStatus s)
{
    switch (s) {
    case GitFileStatus::Modified:   return 'M';
    case GitFileStatus::Added:      return 'A';
    case GitFileStatus::Deleted:    return 'D';
    case GitFileStatus::Renamed:    return 'R';
    case GitFileStatus::Copied:     return 'C';
    case GitFileStatus::Untracked:  return '?';
    case GitFileStatus::Conflicted: return '!';
    default:                        return QChar();
    }
}

static QColor gitStatusColor(GitFileStatus s)
{
    const auto &p = ThemeManager::instance().palette();
    switch (s) {
    case GitFileStatus::Modified:   return p.yellow;
    case GitFileStatus::Added:      return p.green;
    case GitFileStatus::Deleted:    return p.red;
    case GitFileStatus::Renamed:    return p.blue;
    case GitFileStatus::Copied:     return p.blue;
    case GitFileStatus::Untracked:  return p.text_muted;
    case GitFileStatus::Conflicted: return p.peach;
    default:                        return QColor();
    }
}

void ChangedFileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    if (!m_model) return;
    QModelIndex srcIndex = m_proxy ? m_proxy->mapToSource(index) : index;
    QString path = m_model->filePath(srcIndex);

    int rightOffset = 8;

    // --- Git status badge (letter) ---
    if (m_gitStatus && !m_rootPath.isEmpty()) {
        QString relPath = QDir(m_rootPath).relativeFilePath(path);
        bool isDir = m_model->isDir(srcIndex);

        // For files: exact match. For directories: scan for any child with status.
        GitFileStatus displayStatus = GitFileStatus::Unmodified;
        if (!isDir) {
            auto git = m_gitStatus->find(relPath);
            if (git != m_gitStatus->end())
                displayStatus = git.value();
        } else {
            QString prefix = relPath + '/';
            for (auto it = m_gitStatus->constBegin(); it != m_gitStatus->constEnd(); ++it) {
                if (it.key().startsWith(prefix)) {
                    displayStatus = it.value();
                    break;  // any child is enough to mark the directory
                }
            }
        }

        if (displayStatus != GitFileStatus::Unmodified) {
            QChar letter = gitStatusLetter(displayStatus);
            QColor color = gitStatusColor(displayStatus);
            if (!letter.isNull()) {
                painter->save();
                QFont f = option.font;
                f.setPointSize(9);
                f.setBold(true);
                painter->setFont(f);
                painter->setPen(color);
                int x = option.rect.right() - rightOffset - 8;
                int y = option.rect.center().y() + 4;
                painter->drawText(x, y, QString(letter));
                painter->restore();
                rightOffset += 14;
            }
        }
    }

    // --- Claude change dot (existing behavior) ---
    if (m_files) {
        auto it = m_files->find(path);
        if (it != m_files->end()) {
            QColor dotColor;
            const auto &pal = ThemeManager::instance().palette();
            switch (it.value()) {
            case FileChangeType::Modified: dotColor = pal.green; break;
            case FileChangeType::Created:  dotColor = pal.peach; break;
            case FileChangeType::Deleted:  dotColor = pal.red; break;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setBrush(dotColor);
            painter->setPen(Qt::NoPen);
            int y = option.rect.center().y();
            int x = option.rect.right() - rightOffset - 3;
            painter->drawEllipse(QPoint(x, y), 3, 3);
            painter->restore();
        }
    }
}

WorkspaceTree::WorkspaceTree(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerContainer = new QWidget(this);
    m_headerContainer->setFixedHeight(38);
    auto *headerLayout = new QVBoxLayout(m_headerContainer);
    headerLayout->setContentsMargins(10, 4, 10, 2);
    headerLayout->setSpacing(0);

    m_header = new QLabel("EXPLORER", m_headerContainer);
    headerLayout->addWidget(m_header);

    m_headerSubtitle = new QLabel("", m_headerContainer);
    m_headerSubtitle->setVisible(false);
    headerLayout->addWidget(m_headerSubtitle);

    layout->addWidget(m_headerContainer);

    m_model = new QFileSystemModel(this);
    m_model->setIconProvider(new FileIconProvider);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    m_model->setNameFilterDisables(false);

    m_proxy = new FileFilterProxy(this);
    m_proxy->setSourceModel(m_model);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_proxy);
    m_tree->setHeaderHidden(true);
    m_tree->setAnimated(true);
    m_tree->setIndentation(14);
    m_tree->hideColumn(1);
    m_tree->hideColumn(2);
    m_tree->hideColumn(3);

    m_delegate = new ChangedFileDelegate(this);
    m_delegate->setChangedFiles(&m_changedFiles);
    m_delegate->setGitStatus(&m_gitStatus);
    m_delegate->setModel(m_model);
    m_delegate->setProxy(m_proxy);
    m_tree->setItemDelegate(m_delegate);

    layout->addWidget(m_tree);

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeView::customContextMenuRequested,
            this, &WorkspaceTree::onContextMenu);

    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        QModelIndex src = mapToSource(index);
        QString path = m_model->filePath(src);
        if (m_model->isDir(src))
            return;
        emit fileSelected(path);
    });

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &WorkspaceTree::applyThemeColors);
}

void WorkspaceTree::applyThemeColors()
{
    const auto &pal = ThemeManager::instance().palette();
    m_headerContainer->setStyleSheet(QStringLiteral(
        "QWidget { background: %1; border-bottom: 1px solid %2; }")
        .arg(pal.bg_window.name(), pal.border_subtle.name()));
    m_header->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: %1; font-size: 11px; "
        "font-weight: 600; letter-spacing: 0.5px; }")
        .arg(pal.text_secondary.name()));
    m_headerSubtitle->setStyleSheet(QStringLiteral(
        "QLabel { background: transparent; color: %1; font-size: 10px; }")
        .arg(pal.text_faint.name()));
}

void WorkspaceTree::setRootPath(const QString &path)
{
    m_rootPath = path;
    m_model->setRootPath(path);
    m_tree->setRootIndex(m_proxy->mapFromSource(m_model->index(path)));

    QString cleanPath = QDir::cleanPath(path);
    QString folderName = QDir(cleanPath).dirName();
    m_header->setText(folderName.toUpper());
    m_headerSubtitle->setText(cleanPath);
    m_headerSubtitle->setVisible(true);
    m_delegate->setRootPath(path);
}

void WorkspaceTree::markFileChanged(const QString &filePath, FileChangeType type)
{
    m_changedFiles.insert(filePath, type);
    m_tree->viewport()->update();
}

void WorkspaceTree::clearChangeMarkers()
{
    m_changedFiles.clear();
    m_tree->viewport()->update();
}

void WorkspaceTree::setGitFileEntries(const QList<GitFileEntry> &entries)
{
    m_gitStatus.clear();
    for (const auto &e : entries) {
        // Pick the most relevant status to display (prefer work-tree, fall back to index)
        GitFileStatus display = GitFileStatus::Unmodified;
        if (e.workStatus != GitFileStatus::Unmodified)
            display = e.workStatus;
        else if (e.indexStatus != GitFileStatus::Unmodified)
            display = e.indexStatus;

        if (display != GitFileStatus::Unmodified)
            m_gitStatus.insert(e.filePath, display);
    }
    m_tree->viewport()->update();
}

void WorkspaceTree::clearGitStatus()
{
    m_gitStatus.clear();
    m_tree->viewport()->update();
}

QModelIndex WorkspaceTree::mapToSource(const QModelIndex &proxyIndex) const
{
    return m_proxy ? m_proxy->mapToSource(proxyIndex) : proxyIndex;
}

QString WorkspaceTree::contextDirectory(const QModelIndex &index) const
{
    if (!index.isValid())
        return m_rootPath;
    QModelIndex src = mapToSource(index);
    QString path = m_model->filePath(src);
    if (m_model->isDir(src))
        return path;
    return QFileInfo(path).absolutePath();
}

void WorkspaceTree::onContextMenu(const QPoint &pos)
{
    QModelIndex index = m_tree->indexAt(pos);
    QModelIndex srcIndex = mapToSource(index);
    QString targetPath = index.isValid() ? m_model->filePath(srcIndex) : m_rootPath;
    QString parentDir = contextDirectory(index);
    bool isDir = index.isValid() && m_model->isDir(srcIndex);
    bool isFile = index.isValid() && !isDir;

    QMenu menu(this);

    auto *newFileAction = menu.addAction("New File...");
    connect(newFileAction, &QAction::triggered, this, [this, parentDir] {
        createNewFile(parentDir);
    });

    auto *newFolderAction = menu.addAction("New Folder...");
    connect(newFolderAction, &QAction::triggered, this, [this, parentDir] {
        createNewFolder(parentDir);
    });

    if (index.isValid()) {
        menu.addSeparator();

        auto *renameAction = menu.addAction("Rename...");
        connect(renameAction, &QAction::triggered, this, [this, targetPath, isDir] {
            QFileInfo info(targetPath);
            QString oldName = info.fileName();
            bool ok = false;
            QString newName = QInputDialog::getText(
                this, isDir ? "Rename Folder" : "Rename File",
                "New name:", QLineEdit::Normal, oldName, &ok);
            if (!ok || newName.isEmpty() || newName == oldName)
                return;

            QString newPath = info.absolutePath() + "/" + newName;
            if (QFile::exists(newPath)) {
                QMessageBox::warning(this, "Rename",
                    QStringLiteral("'%1' already exists.").arg(newName),
                    QMessageBox::Ok);
                return;
            }
            if (QFile::rename(targetPath, newPath)) {
                ToastManager::instance().show(
                    QStringLiteral("Renamed to %1").arg(newName),
                    ToastType::Success, 2000);
            } else {
                QMessageBox::warning(this, "Rename", "Failed to rename.",
                    QMessageBox::Ok);
            }
        });

        auto *deleteAction = menu.addAction("Delete");
        connect(deleteAction, &QAction::triggered, this, [this, targetPath, isDir] {
            deleteSelected(targetPath, isDir);
        });
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void WorkspaceTree::createNewFile(const QString &parentDir)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "New File",
        "File name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty())
        return;

    QString fullPath = parentDir + "/" + name;
    if (QFile::exists(fullPath)) {
        QMessageBox::warning(this, "New File",
            QStringLiteral("'%1' already exists.").arg(name),
            QMessageBox::Ok);
        return;
    }

    QDir().mkpath(QFileInfo(fullPath).absolutePath());
    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.close();
        emit fileCreated(fullPath);
        emit fileSelected(fullPath);
        ToastManager::instance().show(
            QStringLiteral("Created %1").arg(name), ToastType::Success, 2000);
    } else {
        QMessageBox::warning(this, "New File", "Failed to create file.",
            QMessageBox::Ok);
    }
}

void WorkspaceTree::createNewFolder(const QString &parentDir)
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Folder",
        "Folder name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty())
        return;

    QString fullPath = parentDir + "/" + name;
    if (QDir(fullPath).exists()) {
        QMessageBox::warning(this, "New Folder",
            QStringLiteral("'%1' already exists.").arg(name),
            QMessageBox::Ok);
        return;
    }

    if (QDir().mkpath(fullPath)) {
        emit folderCreated(fullPath);
        ToastManager::instance().show(
            QStringLiteral("Created folder %1").arg(name), ToastType::Success, 2000);
    } else {
        QMessageBox::warning(this, "New Folder", "Failed to create folder.",
            QMessageBox::Ok);
    }
}

void WorkspaceTree::deleteSelected(const QString &path, bool isDir)
{
    QFileInfo info(path);
    QString name = info.fileName();

    QString prompt = isDir
        ? QStringLiteral("Delete folder '%1' and all its contents?\n\nThis cannot be undone.").arg(name)
        : QStringLiteral("Delete file '%1'?\n\nThis cannot be undone.").arg(name);

    auto result = QMessageBox::warning(this,
        isDir ? "Delete Folder" : "Delete File",
        prompt, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result != QMessageBox::Yes)
        return;

    bool success = false;
    if (isDir)
        success = QDir(path).removeRecursively();
    else
        success = QFile::remove(path);

    if (success) {
        if (isDir)
            emit folderDeleted(path);
        else
            emit fileDeleted(path);
        ToastManager::instance().show(
            QStringLiteral("Deleted %1").arg(name), ToastType::Success, 2000);
    } else {
        QMessageBox::warning(this, "Delete",
            QStringLiteral("Failed to delete '%1'.").arg(name),
            QMessageBox::Ok);
    }
}
