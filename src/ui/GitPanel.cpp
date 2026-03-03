#include "ui/GitPanel.h"
#include "ui/FileIconProvider.h"
#include "ui/ThemeManager.h"
#include "ui/ToastManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QFont>
#include <QTimer>

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
    connect(m_git, &GitManager::branchesListed, this, &GitPanel::updateBranches);

    connect(m_git, &GitManager::fetchSucceeded, this, [this] {
        if (m_git) m_git->listBranches();
    });
    connect(m_git, &GitManager::checkoutSucceeded, this, [this] {
        if (m_git) m_git->listBranches();
    });
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void GitPanel::setupUI()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_noRepoPlaceholder = new QWidget(this);
    auto *phLayout = new QVBoxLayout(m_noRepoPlaceholder);
    phLayout->setAlignment(Qt::AlignCenter);
    m_phLabel = new QLabel("Not a git repository", m_noRepoPlaceholder);
    m_phLabel->setAlignment(Qt::AlignCenter);
    phLayout->addWidget(m_phLabel);
    rootLayout->addWidget(m_noRepoPlaceholder);
    m_noRepoPlaceholder->hide();

    m_mainContent = new QWidget(this);
    auto *layout = new QVBoxLayout(m_mainContent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerWidget = new QWidget(m_mainContent);
    m_headerWidget->setFixedHeight(26);
    auto *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(8, 0, 4, 0);
    headerLayout->setSpacing(4);

    m_branchLabel = new QLabel("", m_headerWidget);
    headerLayout->addWidget(m_branchLabel);
    headerLayout->addStretch();

    m_fetchBtn = new QPushButton("\xe2\xac\x87", m_headerWidget);
    m_fetchBtn->setFixedSize(20, 18);
    m_fetchBtn->setToolTip("Fetch from origin");
    headerLayout->addWidget(m_fetchBtn);
    connect(m_fetchBtn, &QPushButton::clicked, this, &GitPanel::onFetch);

    m_pushBtn = new QPushButton("\xe2\xac\x86", m_headerWidget);
    m_pushBtn->setFixedSize(20, 18);
    m_pushBtn->setToolTip("Push to origin");
    headerLayout->addWidget(m_pushBtn);
    connect(m_pushBtn, &QPushButton::clicked, this, &GitPanel::onPush);

    m_refreshBtn = new QPushButton("\xe2\x86\xbb", m_headerWidget);
    m_refreshBtn->setFixedSize(20, 18);
    m_refreshBtn->setToolTip("Refresh");
    headerLayout->addWidget(m_refreshBtn);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this] {
        if (m_git) m_git->refreshStatus();
    });

    layout->addWidget(m_headerWidget);

    // Branch tree — shows LOCAL and REMOTE branches
    m_branchTree = new QTreeWidget(m_mainContent);
    m_branchTree->setHeaderHidden(true);
    m_branchTree->setRootIsDecorated(true);
    m_branchTree->setIndentation(14);
    m_branchTree->setAnimated(true);
    m_branchTree->setMaximumHeight(200);
    connect(m_branchTree, &QTreeWidget::itemDoubleClicked,
            this, &GitPanel::onBranchDoubleClicked);
    layout->addWidget(m_branchTree);

    m_tree = new QTreeWidget(m_mainContent);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setAnimated(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemClicked, this, &GitPanel::onItemClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &GitPanel::onItemContextMenu);

    m_commitArea = new QWidget(m_mainContent);
    auto *commitLayout = new QVBoxLayout(m_commitArea);
    commitLayout->setContentsMargins(6, 6, 6, 6);
    commitLayout->setSpacing(4);

    m_commitMsg = new QTextEdit(m_commitArea);
    m_commitMsg->setPlaceholderText("Commit message...");
    m_commitMsg->setFixedHeight(60);
    commitLayout->addWidget(m_commitMsg);

    auto *btnRow1 = new QHBoxLayout();
    btnRow1->setSpacing(4);

    m_stageAllBtn = new QPushButton("Stage All", m_commitArea);
    btnRow1->addWidget(m_stageAllBtn);
    connect(m_stageAllBtn, &QPushButton::clicked, this, &GitPanel::onStageAll);

    m_commitBtn = new QPushButton("Commit", m_commitArea);
    btnRow1->addWidget(m_commitBtn);
    connect(m_commitBtn, &QPushButton::clicked, this, &GitPanel::onCommit);

    commitLayout->addLayout(btnRow1);

    auto *btnRow2 = new QHBoxLayout();
    btnRow2->setSpacing(4);

    m_unstageAllBtn = new QPushButton("Unstage All", m_commitArea);
    btnRow2->addWidget(m_unstageAllBtn);
    connect(m_unstageAllBtn, &QPushButton::clicked, this, &GitPanel::onUnstageAll);

    m_discardAllBtn = new QPushButton("Discard All", m_commitArea);
    btnRow2->addWidget(m_discardAllBtn);
    connect(m_discardAllBtn, &QPushButton::clicked, this, &GitPanel::onDiscardAll);

    commitLayout->addLayout(btnRow2);
    layout->addWidget(m_commitArea);
    rootLayout->addWidget(m_mainContent);

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &GitPanel::applyThemeColors);
}

void GitPanel::applyThemeColors()
{
    const auto &pal = ThemeManager::instance().palette();

    m_phLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;")
        .arg(pal.text_muted.name()));

    m_headerWidget->setStyleSheet(QStringLiteral(
        "QWidget { background: %1; border-bottom: 1px solid %2; }")
        .arg(pal.bg_window.name(), pal.border_standard.name()));

    m_branchLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; font-weight: bold; background: transparent; }")
        .arg(pal.green.name()));

    const QString iconBtnStyle = QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; font-size: 14px; }"
        "QPushButton:hover { color: %2; }")
        .arg(pal.text_muted.name(), pal.text_primary.name());

    m_refreshBtn->setStyleSheet(iconBtnStyle);
    m_fetchBtn->setStyleSheet(iconBtnStyle);
    m_pushBtn->setStyleSheet(iconBtnStyle);

    m_branchTree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: %1; border: none; border-bottom: 1px solid %2; }"
        "QTreeWidget::item { padding: 1px 4px; min-height: 20px; }"
        "QTreeWidget::item:hover { background: rgba(255,255,255,0.04); }"
        "QTreeWidget::item:selected { background: %3; color: %4; }")
        .arg(pal.bg_window.name(), pal.border_subtle.name(),
             pal.bg_raised.name(), pal.text_primary.name()));

    m_commitArea->setStyleSheet(QStringLiteral("QWidget { background: %1; }")
        .arg(pal.bg_window.name()));

    m_commitMsg->setStyleSheet(QStringLiteral(
        "QTextEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 4px; padding: 4px 6px; font-size: 12px; font-family: 'Inter'; }"
        "QTextEdit:focus { border-color: %4; }")
        .arg(pal.bg_window.name(), pal.text_primary.name(),
             pal.border_standard.name(), pal.border_focus.name()));

    m_commitBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
        "padding: 4px 10px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:disabled { background: %4; color: %5; }")
        .arg(pal.success_btn_bg.name(), pal.text_primary.name(),
             pal.success_btn_hover.name(), pal.bg_window.name(), pal.text_faint.name()));

    m_discardAllBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
        "padding: 4px 10px; font-size: 11px; }"
        "QPushButton:hover { background: %3; }")
        .arg(pal.error_btn_bg.name(), pal.text_primary.name(), pal.error_btn_hover.name()));
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
    m_branchLabel->setText(branch);
}

void GitPanel::updateBranches(const QList<GitBranchEntry> &branches)
{
    rebuildBranchTree(branches);
}

void GitPanel::rebuildBranchTree(const QList<GitBranchEntry> &branches)
{
    m_branchTree->clear();

    const auto &pal = ThemeManager::instance().palette();
    QFont sectionFont;
    sectionFont.setPointSize(9);
    sectionFont.setBold(true);

    m_localRoot = new QTreeWidgetItem(m_branchTree);
    m_localRoot->setText(0, "LOCAL");
    m_localRoot->setFlags(Qt::ItemIsEnabled);
    m_localRoot->setFont(0, sectionFont);
    m_localRoot->setForeground(0, pal.text_muted);
    m_localRoot->setExpanded(true);

    m_remoteRoot = new QTreeWidgetItem(m_branchTree);
    m_remoteRoot->setText(0, "REMOTE");
    m_remoteRoot->setFlags(Qt::ItemIsEnabled);
    m_remoteRoot->setFont(0, sectionFont);
    m_remoteRoot->setForeground(0, pal.text_muted);
    m_remoteRoot->setExpanded(true);

    QMap<QString, QTreeWidgetItem *> remoteGroups;

    for (const auto &b : branches) {
        if (b.isLocal) {
            auto *item = new QTreeWidgetItem(m_localRoot);
            item->setData(0, Qt::UserRole, b.name);
            item->setData(0, Qt::UserRole + 1, true);
            if (b.isCurrent) {
                item->setText(0, QStringLiteral("\u25cf %1").arg(b.name));
                QFont f = item->font(0);
                f.setBold(true);
                item->setFont(0, f);
                item->setForeground(0, pal.green);
            } else {
                item->setText(0, b.name);
                item->setForeground(0, pal.text_secondary);
            }
        } else {
            const QString &remoteName = b.remote.isEmpty() ? QStringLiteral("origin") : b.remote;
            if (!remoteGroups.contains(remoteName)) {
                auto *group = new QTreeWidgetItem(m_remoteRoot);
                group->setText(0, remoteName);
                group->setFlags(Qt::ItemIsEnabled);
                group->setForeground(0, pal.blue);
                group->setExpanded(true);
                remoteGroups[remoteName] = group;
            }
            auto *item = new QTreeWidgetItem(remoteGroups[remoteName]);
            // Strip "origin/" prefix for display
            QString display = b.name;
            int slash = display.indexOf('/');
            if (slash >= 0)
                display = display.mid(slash + 1);
            item->setText(0, display);
            item->setData(0, Qt::UserRole, b.name);
            item->setData(0, Qt::UserRole + 1, false);
            item->setForeground(0, pal.text_muted);
        }
    }

    // Hide the remote section if empty
    m_remoteRoot->setHidden(m_remoteRoot->childCount() == 0);
}

void GitPanel::onPush()
{
    if (m_git) m_git->push();
}

void GitPanel::onFetch()
{
    if (m_git) m_git->fetch();
}

void GitPanel::onBranchDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item || item == m_localRoot || item == m_remoteRoot)
        return;
    // Skip remote group headers (have children but no UserRole data)
    if (item->childCount() > 0)
        return;

    bool isLocal = item->data(0, Qt::UserRole + 1).toBool();
    QString branchName = item->data(0, Qt::UserRole).toString();
    if (branchName.isEmpty() || !m_git)
        return;

    if (isLocal) {
        m_git->checkoutBranch(branchName);
    } else {
        // For remote branches checkout the local name (strip "origin/")
        int slash = branchName.indexOf('/');
        m_git->checkoutBranch(slash >= 0 ? branchName.mid(slash + 1) : branchName);
    }
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
    m_stagedRoot->setForeground(0, ThemeManager::instance().palette().text_muted);
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
        item->setIcon(0, FileIconProvider::iconForFile(e.filePath));
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
    m_changesRoot->setForeground(0, ThemeManager::instance().palette().text_muted);
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
        item->setIcon(0, FileIconProvider::iconForFile(e.filePath));
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
    const auto &p = ThemeManager::instance().palette();
    switch (status) {
    case GitFileStatus::Modified:   return p.yellow;
    case GitFileStatus::Added:      return p.green;
    case GitFileStatus::Deleted:    return p.red;
    case GitFileStatus::Renamed:    return p.blue;
    case GitFileStatus::Copied:     return p.blue;
    case GitFileStatus::Untracked:  return p.text_muted;
    case GitFileStatus::Conflicted: return p.peach;
    default:                        return p.text_secondary;
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
    // Styled via QSS

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

    connect(m_git, &GitManager::commitSucceeded, this,
            [this](const QString &hash, const QString &) {
        m_commitMsg->clear();

        // Flash the commit button green
        const auto &pal = ThemeManager::instance().palette();
        const QString successStyle = QStringLiteral(
            "QPushButton { background: %1; color: %2; border: none; "
            "border-radius: 4px; padding: 4px 10px; font-size: 11px; font-weight: bold; }")
            .arg(pal.success_btn_bg.name(), pal.green.name());
        const QString normalStyle = QStringLiteral(
            "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
            "padding: 4px 10px; font-size: 11px; font-weight: bold; }"
            "QPushButton:hover { background: %3; }"
            "QPushButton:disabled { background: %4; color: %5; }")
            .arg(pal.success_btn_bg.name(),
                 pal.text_primary.name(),
                 pal.success_btn_hover.name(),
                 pal.bg_window.name(),
                 pal.text_faint.name());

        m_commitBtn->setText(QStringLiteral("\u2713  Committed!"));
        m_commitBtn->setStyleSheet(successStyle);

        QTimer::singleShot(2500, this, [this, normalStyle] {
            m_commitBtn->setText("Commit");
            m_commitBtn->setStyleSheet(normalStyle);
        });

        ToastManager::instance().show(
            QStringLiteral("Committed %1").arg(hash.left(7)),
            ToastType::Success, 2500);
    }, Qt::SingleShotConnection);
}
