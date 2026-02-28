#include "ui/WorkspaceTree.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>

void ChangedFileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    if (m_files && m_model) {
        QString path = m_model->filePath(index);
        auto it = m_files->find(path);
        if (it != m_files->end()) {
            QColor dotColor;
            switch (it.value()) {
            case FileChangeType::Modified: dotColor = QColor("#a6e3a1"); break; // green
            case FileChangeType::Created:  dotColor = QColor("#fab387"); break; // orange
            case FileChangeType::Deleted:  dotColor = QColor("#f38ba8"); break; // red
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setBrush(dotColor);
            painter->setPen(Qt::NoPen);
            int y = option.rect.center().y();
            int x = option.rect.right() - 10;
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
    header->setFixedHeight(22);
    header->setStyleSheet(
        "QLabel { background: #161616; color: #555; font-size: 10px; "
        "font-weight: bold; letter-spacing: 1px; padding-left: 8px; "
        "border-bottom: 1px solid #222; }");
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
    m_model->setRootPath(path);
    m_tree->setRootIndex(m_model->index(path));
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
