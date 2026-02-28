#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include "core/GitManager.h"

class GitPanel : public QWidget {
    Q_OBJECT
public:
    explicit GitPanel(QWidget *parent = nullptr);

    void setGitManager(GitManager *mgr);

public slots:
    void updateStatus(const QList<GitFileEntry> &entries);
    void updateBranch(const QString &branch);
    void showNotARepo();

signals:
    void fileClicked(const QString &filePath, bool staged);
    void requestOpenFile(const QString &filePath);

private slots:
    void onStageAll();
    void onUnstageAll();
    void onDiscardAll();
    void onCommit();
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onItemContextMenu(const QPoint &pos);

private:
    void setupUI();
    void rebuildTree(const QList<GitFileEntry> &entries);
    QString statusChar(GitFileStatus status) const;
    QColor statusColor(GitFileStatus status) const;

    GitManager *m_git = nullptr;

    QLabel *m_branchLabel;
    QPushButton *m_refreshBtn;
    QTreeWidget *m_tree;
    QTreeWidgetItem *m_stagedRoot = nullptr;
    QTreeWidgetItem *m_changesRoot = nullptr;
    QTextEdit *m_commitMsg;
    QPushButton *m_stageAllBtn;
    QPushButton *m_unstageAllBtn;
    QPushButton *m_commitBtn;
    QPushButton *m_discardAllBtn;
    QWidget *m_noRepoPlaceholder;
    QWidget *m_mainContent;

    QList<GitFileEntry> m_currentEntries;
};
