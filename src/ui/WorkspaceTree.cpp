#include "ui/WorkspaceTree.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QDir>

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
    switch (s) {
    case GitFileStatus::Modified:   return QColor("#f9e2af");
    case GitFileStatus::Added:      return QColor("#a6e3a1");
    case GitFileStatus::Deleted:    return QColor("#f38ba8");
    case GitFileStatus::Renamed:    return QColor("#89b4fa");
    case GitFileStatus::Copied:     return QColor("#89b4fa");
    case GitFileStatus::Untracked:  return QColor("#6c7086");
    case GitFileStatus::Conflicted: return QColor("#fab387");
    default:                        return QColor();
    }
}

void ChangedFileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    if (!m_model) return;
    QString path = m_model->filePath(index);

    int rightOffset = 8;

    // --- Git status badge (letter) ---
    if (m_gitStatus && !m_rootPath.isEmpty()) {
        QString relPath = QDir(m_rootPath).relativeFilePath(path);
        bool isDir = m_model->isDir(index);

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
            switch (it.value()) {
            case FileChangeType::Modified: dotColor = QColor("#a6e3a1"); break;
            case FileChangeType::Created:  dotColor = QColor("#fab387"); break;
            case FileChangeType::Deleted:  dotColor = QColor("#f38ba8"); break;
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

    auto *header = new QLabel("  EXPLORER", this);
    header->setFixedHeight(26);
    header->setStyleSheet(
        "QLabel { background: #0e0e0e; color: #6c7086; font-size: 11px; "
        "font-weight: bold; letter-spacing: 1px; padding-left: 8px; "
        "border-bottom: 1px solid #2a2a2a; }");
    layout->addWidget(header);

    m_model = new QFileSystemModel(this);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_model->setNameFilterDisables(false);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
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
    m_tree->setItemDelegate(m_delegate);

    layout->addWidget(m_tree);

    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        QString path = m_model->filePath(index);
        if (m_model->isDir(index))
            return;
        emit fileSelected(path);
    });
}

void WorkspaceTree::setRootPath(const QString &path)
{
    m_rootPath = path;
    m_model->setRootPath(path);
    m_tree->setRootIndex(m_model->index(path));
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
