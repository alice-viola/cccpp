#include "ui/ContextPopup.h"
#include "ui/ThemeManager.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

ContextPopup::ContextPopup(QWidget *parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setFixedWidth(320);
    setMaximumHeight(280);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(0);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        int row = m_list->row(item);
        if (row >= 0 && row < m_items.size()) {
            auto &ci = m_items[row];
            emit itemSelected(ci.displayName, ci.fullPath);
        }
    });

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ContextPopup::applyThemeColors);
}

void ContextPopup::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; color: %1; border: none; outline: none; }"
        "QListWidget::item { padding: 4px 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background: %2; color: %3; }"
        "QListWidget::item:hover:!selected { background: %4; }")
        .arg(p.text_primary.name(), p.bg_raised.name(),
             p.text_primary.name(), p.hover_raised.name()));
}

void ContextPopup::paintEvent(QPaintEvent *)
{
    auto &p = ThemeManager::instance().palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
    painter.fillPath(path, p.bg_surface);
    painter.setPen(QPen(p.border_standard, 1));
    painter.drawPath(path);
}

void ContextPopup::setWorkspacePath(const QString &path)
{
    m_workspacePath = path;
    while (m_workspacePath.endsWith('/') && m_workspacePath.length() > 1)
        m_workspacePath.chop(1);
}

void ContextPopup::setOpenFiles(const QStringList &files)
{
    m_openFiles = files;
}

void ContextPopup::setRecentFiles(const QStringList &files)
{
    m_recentFiles = files;
}

void ContextPopup::updateFilter(const QString &filter)
{
    rebuild(filter);
}

void ContextPopup::rebuild(const QString &filter)
{
    m_list->clear();
    m_items.clear();

    auto &tm = ThemeManager::instance();
    QString mutedHex = tm.hex("text_muted");
    QString blueHex = tm.hex("blue");
    QString greenHex = tm.hex("green");
    QString peachHex = tm.hex("peach");

    auto addItem = [&](const ContextItem &ci) {
        QString colorHex;
        QString tagText;
        switch (ci.type) {
        case ContextItem::OpenTab:   colorHex = greenHex; tagText = "open"; break;
        case ContextItem::RecentFile: colorHex = peachHex; tagText = "recent"; break;
        case ContextItem::Folder:    colorHex = blueHex; tagText = "folder"; break;
        default:                     colorHex = mutedHex; tagText = ""; break;
        }

        auto *item = new QListWidgetItem(m_list);
        QString label = ci.displayName;
        if (!tagText.isEmpty())
            label += QStringLiteral("  [%1]").arg(tagText);
        item->setText(label);
        item->setToolTip(ci.fullPath);
        m_items.append(ci);
    };

    // Open tabs first (highest priority)
    for (const QString &f : m_openFiles) {
        QFileInfo fi(f);
        QString rel = f;
        if (!m_workspacePath.isEmpty() && f.startsWith(m_workspacePath))
            rel = f.mid(m_workspacePath.length() + 1);
        if (!filter.isEmpty() && !rel.contains(filter, Qt::CaseInsensitive))
            continue;
        ContextItem ci;
        ci.type = ContextItem::OpenTab;
        ci.displayName = rel;
        ci.fullPath = f;
        addItem(ci);
    }

    // Recent files (not already in open tabs)
    QSet<QString> seen;
    for (const auto &f : m_openFiles) seen.insert(f);
    for (const QString &f : m_recentFiles) {
        if (seen.contains(f)) continue;
        seen.insert(f);
        QFileInfo fi(f);
        QString rel = f;
        if (!m_workspacePath.isEmpty() && f.startsWith(m_workspacePath))
            rel = f.mid(m_workspacePath.length() + 1);
        if (!filter.isEmpty() && !rel.contains(filter, Qt::CaseInsensitive))
            continue;
        ContextItem ci;
        ci.type = ContextItem::RecentFile;
        ci.displayName = rel;
        ci.fullPath = f;
        addItem(ci);
    }

    // Workspace files — root-level first, then subdirectories
    if (!m_workspacePath.isEmpty()) {
        int maxResults = 100 - m_items.size();

        // Pass 1: root-level files and folders (most relevant)
        if (maxResults > 0) {
            QDir rootDir(m_workspacePath);
            QFileInfoList rootEntries = rootDir.entryInfoList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &fi : rootEntries) {
                if (maxResults <= 0) break;
                QString fullPath = fi.absoluteFilePath();
                if (seen.contains(fullPath)) continue;

                QString rel = fi.fileName();
                if (rel.startsWith(".git") || rel == "node_modules" ||
                    rel == "build" || rel == ".cache")
                    continue;

                if (!filter.isEmpty() && !rel.contains(filter, Qt::CaseInsensitive))
                    continue;

                ContextItem ci;
                ci.type = fi.isDir() ? ContextItem::Folder : ContextItem::File;
                ci.displayName = rel;
                ci.fullPath = fullPath;
                addItem(ci);
                seen.insert(fullPath);
                --maxResults;
            }
        }

        // Pass 2: files inside subdirectories (deeper results)
        if (maxResults > 0) {
            QDirIterator it(m_workspacePath,
                            QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext() && maxResults > 0) {
                it.next();
                QString fullPath = it.filePath();
                if (seen.contains(fullPath)) continue;

                QString rel = fullPath.mid(m_workspacePath.length() + 1);
                if (rel.startsWith(".git/") || rel.startsWith("node_modules/") ||
                    rel.startsWith("build/") || rel.startsWith(".cache/") ||
                    rel.startsWith("__pycache__/") || rel.startsWith("third_party/"))
                    continue;

                if (!filter.isEmpty() && !rel.contains(filter, Qt::CaseInsensitive))
                    continue;

                ContextItem ci;
                ci.type = ContextItem::File;
                ci.displayName = rel;
                ci.fullPath = fullPath;
                addItem(ci);
                seen.insert(fullPath);
                --maxResults;
            }
        }
    }

    // Auto-select first item
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);

    int h = qMin(m_list->count() * 28 + 12, 280);
    setFixedHeight(qMax(h, 40));
}

void ContextPopup::selectNext()
{
    int cur = m_list->currentRow();
    if (cur < m_list->count() - 1)
        m_list->setCurrentRow(cur + 1);
}

void ContextPopup::selectPrevious()
{
    int cur = m_list->currentRow();
    if (cur > 0)
        m_list->setCurrentRow(cur - 1);
}

QString ContextPopup::acceptSelection()
{
    int row = m_list->currentRow();
    if (row >= 0 && row < m_items.size()) {
        auto &ci = m_items[row];
        emit itemSelected(ci.displayName, ci.fullPath);
        return ci.displayName;
    }
    return {};
}

bool ContextPopup::hasSelection() const
{
    return m_list->currentRow() >= 0;
}

int ContextPopup::itemCount() const
{
    return m_list->count();
}
