#include "ui/GitPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QFont>

GitPanel::GitPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void GitPanel::setGitManager(GitManager *mgr)
{
    m_git = mgr;
    if (!m_git) return;

    connect(m_git, &GitManager::statusChanged, this, &GitPanel::updateStatus);
    connect(m_git, &GitManager::branchChanged, this, &GitPanel::updateBranch);
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void GitPanel::setupUI()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // --- Placeholder for non-git repos ---
    m_noRepoPlaceholder = new QWidget(this);
    auto *phLayout = new QVBoxLayout(m_noRepoPlaceholder);
    phLayout->setAlignment(Qt::AlignCenter);
    auto *phLabel = new QLabel("Not a git repository", m_noRepoPlaceholder);
    phLabel->setStyleSheet("color: #555; font-size: 12px;");
    phLabel->setAlignment(Qt::AlignCenter);
    phLayout->addWidget(phLabel);
    rootLayout->addWidget(m_noRepoPlaceholder);
    m_noRepoPlaceholder->hide();

    // --- Main content ---
    m_mainContent = new QWidget(this);
    auto *layout = new QVBoxLayout(m_mainContent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header: branch + refresh
    auto *headerWidget = new QWidget(m_mainContent);
    headerWidget->setFixedHeight(26);
    headerWidget->setStyleSheet(
        "QWidget { background: #161616; border-bottom: 1px solid #222; }");
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(8, 0, 4, 0);
    headerLayout->setSpacing(4);

    m_branchLabel = new QLabel("", headerWidget);
    m_branchLabel->setStyleSheet(
        "QLabel { color: #a6e3a1; font-size: 11px; font-weight: bold; background: transparent; }");
    headerLayout->addWidget(m_branchLabel);

    headerLayout->addStretch();

    m_refreshBtn = new QPushButton("\xe2\x86\xbb", headerWidget);
    m_refreshBtn->setFixedSize(20, 18);
    m_refreshBtn->setToolTip("Refresh");
    m_refreshBtn->setStyleSheet(
        "QPushButton { background: transparent; color: #888; border: none; font-size: 14px; }"
        "QPushButton:hover { color: #cdd6f4; }");
    headerLayout->addWidget(m_refreshBtn);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this] {
        if (m_git) m_git->refreshStatus();
    });

    layout->addWidget(headerWidget);

    // File tree
    m_tree = new QTreeWidget(m_mainContent);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setAnimated(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setStyleSheet(
        "QTreeWidget { background: #0e0e0e; color: #bac2de; border: none; font-size: 12px; outline: none; }"
        "QTreeWidget::item { padding: 2px 0; }"
        "QTreeWidget::item:selected { background: #2a2a2a; color: #cdd6f4; }"
        "QTreeWidget::item:hover { background: #141414; }"
        "QTreeWidget::branch { background: #0e0e0e; }");
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemClicked, this, &GitPanel::onItemClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &GitPanel::onItemContextMenu);

    // Commit area
    auto *commitArea = new QWidget(m_mainContent);
    commitArea->setStyleSheet("QWidget { background: #141414; }");
    auto *commitLayout = new QVBoxLayout(commitArea);
    commitLayout->setContentsMargins(6, 6, 6, 6);
    commitLayout->setSpacing(4);

    m_commitMsg = new QTextEdit(commitArea);
    m_commitMsg->setPlaceholderText("Commit message...");
    m_commitMsg->setFixedHeight(60);
    m_commitMsg->setStyleSheet(
        "QTextEdit { background: #1a1a1a; color: #cdd6f4; border: 1px solid #2a2a2a; "
        "border-radius: 4px; padding: 4px 6px; font-size: 12px; font-family: 'Helvetica Neue', sans-serif; }");
    commitLayout->addWidget(m_commitMsg);

    auto *btnRow1 = new QHBoxLayout();
    btnRow1->setSpacing(4);

    m_stageAllBtn = new QPushButton("Stage All", commitArea);
    m_stageAllBtn->setStyleSheet(
        "QPushButton { background: #2a2a2a; color: #cdd6f4; border: none; border-radius: 3px; "
        "padding: 3px 8px; font-size: 11px; }"
        "QPushButton:hover { background: #3a3a3a; }");
    btnRow1->addWidget(m_stageAllBtn);
    connect(m_stageAllBtn, &QPushButton::clicked, this, &GitPanel::onStageAll);

    m_commitBtn = new QPushButton("Commit", commitArea);
    m_commitBtn->setStyleSheet(
        "QPushButton { background: #2d5a27; color: #cdd6f4; border: none; border-radius: 3px; "
        "padding: 3px 8px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: #3a6e32; }"
        "QPushButton:disabled { background: #1a1a1a; color: #3a3a3a; }");
    btnRow1->addWidget(m_commitBtn);
    connect(m_commitBtn, &QPushButton::clicked, this, &GitPanel::onCommit);

    commitLayout->addLayout(btnRow1);

    auto *btnRow2 = new QHBoxLayout();
    btnRow2->setSpacing(4);

    m_unstageAllBtn = new QPushButton("Unstage All", commitArea);
    m_unstageAllBtn->setStyleSheet(
        "QPushButton { background: #2a2a2a; color: #cdd6f4; border: none; border-radius: 3px; "
        "padding: 3px 8px; font-size: 11px; }"
        "QPushButton:hover { background: #3a3a3a; }");
    btnRow2->addWidget(m_unstageAllBtn);
    connect(m_unstageAllBtn, &QPushButton::clicked, this, &GitPanel::onUnstageAll);

    m_discardAllBtn = new QPushButton("Discard All", commitArea);
    m_discardAllBtn->setStyleSheet(
        "QPushButton { background: #5a2727; color: #cdd6f4; border: none; border-radius: 3px; "
        "padding: 3px 8px; font-size: 11px; }"
        "QPushButton:hover { background: #6e3232; }");
    btnRow2->addWidget(m_discardAllBtn);
    connect(m_discardAllBtn, &QPushButton::clicked, this, &GitPanel::onDiscardAll);

    commitLayout->addLayout(btnRow2);

    layout->addWidget(commitArea);

    rootLayout->addWidget(m_mainContent);
}

// ---------------------------------------------------------------------------
// Status updates
// ---------------------------------------------------------------------------

void GitPanel::updateStatus(const QList<GitFileEntry> &entries)
{
    m_currentEntries = entries;
    m_noRepoPlaceholder->hide();
    m_mainContent->show();
    rebuildTree(entries);

    // Enable/disable commit button based on staged files
    bool hasStaged = false;
    for (const auto &e : entries) {
        if (e.indexStatus != GitFileStatus::Unmodified &&
            e.indexStatus != GitFileStatus::Untracked) {
            hasStaged = true;
            break;
        }
    }
    m_commitBtn->setEnabled(hasStaged);
}

void GitPanel::updateBranch(const QString &branch)
{
    m_branchLabel->setText(QStringLiteral("\xe2\x8e\x87 %1").arg(branch));
}

void GitPanel::showNotARepo()
{
    m_noRepoPlaceholder->show();
    m_mainContent->hide();
}

// ---------------------------------------------------------------------------
// Tree building
// ---------------------------------------------------------------------------

void GitPanel::rebuildTree(const QList<GitFileEntry> &entries)
{
    m_tree->clear();

    // Count staged and unstaged
    int stagedCount = 0;
    int changesCount = 0;
    for (const auto &e : entries) {
        if (e.indexStatus != GitFileStatus::Unmodified &&
            e.indexStatus != GitFileStatus::Untracked)
            stagedCount++;
        if (e.workStatus != GitFileStatus::Unmodified)
            changesCount++;
    }

    // --- Staged Changes ---
    m_stagedRoot = new QTreeWidgetItem(m_tree);
    m_stagedRoot->setText(0, QStringLiteral("STAGED CHANGES (%1)").arg(stagedCount));
    m_stagedRoot->setFlags(Qt::ItemIsEnabled);
    QFont sectionFont;
    sectionFont.setPointSize(10);
    sectionFont.setBold(true);
    m_stagedRoot->setFont(0, sectionFont);
    m_stagedRoot->setForeground(0, QColor("#888"));
    m_stagedRoot->setExpanded(true);

    for (const auto &e : entries) {
        if (e.indexStatus == GitFileStatus::Unmodified ||
            e.indexStatus == GitFileStatus::Untracked)
            continue;

        auto *item = new QTreeWidgetItem(m_stagedRoot);
        QString label = QStringLiteral("%1  %2")
                            .arg(statusChar(e.indexStatus))
                            .arg(e.filePath);
        item->setText(0, label);
        item->setForeground(0, statusColor(e.indexStatus));
        item->setData(0, Qt::UserRole, e.filePath);
        item->setData(0, Qt::UserRole + 1, true); // staged flag
        item->setToolTip(0, e.filePath);
    }

    // --- Changes (unstaged + untracked) ---
    m_changesRoot = new QTreeWidgetItem(m_tree);
    m_changesRoot->setText(0, QStringLiteral("CHANGES (%1)").arg(changesCount));
    m_changesRoot->setFlags(Qt::ItemIsEnabled);
    m_changesRoot->setFont(0, sectionFont);
    m_changesRoot->setForeground(0, QColor("#888"));
    m_changesRoot->setExpanded(true);

    for (const auto &e : entries) {
        if (e.workStatus == GitFileStatus::Unmodified)
            continue;

        auto *item = new QTreeWidgetItem(m_changesRoot);
        GitFileStatus displayStatus = (e.workStatus == GitFileStatus::Untracked)
                                          ? GitFileStatus::Untracked
                                          : e.workStatus;
        QString label = QStringLiteral("%1  %2")
                            .arg(statusChar(displayStatus))
                            .arg(e.filePath);
        item->setText(0, label);
        item->setForeground(0, statusColor(displayStatus));
        item->setData(0, Qt::UserRole, e.filePath);
        item->setData(0, Qt::UserRole + 1, false); // not staged
        item->setToolTip(0, e.filePath);
    }
}

QString GitPanel::statusChar(GitFileStatus status) const
{
    switch (status) {
    case GitFileStatus::Modified:   return "M";
    case GitFileStatus::Added:      return "A";
    case GitFileStatus::Deleted:    return "D";
    case GitFileStatus::Renamed:    return "R";
    case GitFileStatus::Copied:     return "C";
    case GitFileStatus::Untracked:  return "?";
    case GitFileStatus::Conflicted: return "!";
    case GitFileStatus::Ignored:    return "I";
    default:                        return " ";
    }
}

QColor GitPanel::statusColor(GitFileStatus status) const
{
    switch (status) {
    case GitFileStatus::Modified:   return QColor("#f9e2af"); // yellow
    case GitFileStatus::Added:      return QColor("#a6e3a1"); // green
    case GitFileStatus::Deleted:    return QColor("#f38ba8"); // red
    case GitFileStatus::Renamed:    return QColor("#89b4fa"); // blue
    case GitFileStatus::Copied:     return QColor("#89b4fa");
    case GitFileStatus::Untracked:  return QColor("#6c7086"); // grey
    case GitFileStatus::Conflicted: return QColor("#fab387"); // orange
    default:                        return QColor("#bac2de");
    }
}

// ---------------------------------------------------------------------------
// Item interaction
// ---------------------------------------------------------------------------

void GitPanel::onItemClicked(QTreeWidgetItem *item, int)
{
    if (item == m_stagedRoot || item == m_changesRoot)
        return;

    QString filePath = item->data(0, Qt::UserRole).toString();
    bool staged = item->data(0, Qt::UserRole + 1).toBool();

    if (!filePath.isEmpty())
        emit fileClicked(filePath, staged);
}

void GitPanel::onItemContextMenu(const QPoint &pos)
{
    auto *item = m_tree->itemAt(pos);
    if (!item || item == m_stagedRoot || item == m_changesRoot)
        return;

    QString filePath = item->data(0, Qt::UserRole).toString();
    bool staged = item->data(0, Qt::UserRole + 1).toBool();

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #1a1a1a; color: #cdd6f4; border: 1px solid #2a2a2a; border-radius: 4px; padding: 4px; }"
        "QMenu::item { padding: 4px 16px; border-radius: 3px; }"
        "QMenu::item:selected { background: #2a2a2a; }");

    if (staged) {
        menu.addAction("Unstage", [this, filePath] {
            if (m_git) m_git->unstageFile(filePath);
        });
    } else {
        menu.addAction("Stage", [this, filePath] {
            if (m_git) m_git->stageFile(filePath);
        });

        // Only show discard for tracked files
        bool isUntracked = false;
        for (const auto &e : qAsConst(m_currentEntries)) {
            if (e.filePath == filePath && e.workStatus == GitFileStatus::Untracked) {
                isUntracked = true;
                break;
            }
        }

        menu.addAction("Discard Changes", [this, filePath, isUntracked] {
            QString msg = isUntracked
                              ? QStringLiteral("Delete untracked file '%1'?\n\nThis cannot be undone.")
                              : QStringLiteral("Discard changes to '%1'?\n\nThis cannot be undone.");
            auto result = QMessageBox::warning(this, "Discard Changes",
                                               msg.arg(filePath),
                                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (result == QMessageBox::Yes && m_git)
                m_git->discardFile(filePath);
        });
    }

    menu.addSeparator();
    menu.addAction("Open File", [this, filePath] {
        emit requestOpenFile(filePath);
    });

    menu.exec(m_tree->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Bulk actions
// ---------------------------------------------------------------------------

void GitPanel::onStageAll()
{
    if (m_git) m_git->stageAll();
}

void GitPanel::onUnstageAll()
{
    if (m_git) m_git->unstageAll();
}

void GitPanel::onDiscardAll()
{
    auto result = QMessageBox::warning(
        this, "Discard All Changes",
        "Discard ALL working tree changes and delete untracked files?\n\n"
        "This cannot be undone.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result == QMessageBox::Yes && m_git)
        m_git->discardAll();
}

void GitPanel::onCommit()
{
    if (!m_git) return;

    QString message = m_commitMsg->toPlainText().trimmed();
    if (message.isEmpty()) {
        m_commitMsg->setFocus();
        return;
    }

    m_git->commit(message);

    connect(m_git, &GitManager::commitSucceeded, this, [this](const QString &, const QString &) {
        m_commitMsg->clear();
    }, Qt::SingleShotConnection);
}
