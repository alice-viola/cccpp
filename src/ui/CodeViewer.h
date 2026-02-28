#pragma once

#include <QWidget>
#include <QLabel>
#include <QTabWidget>
#include <QMap>
#include <QSet>
#include <QFileSystemWatcher>
#include <QPushButton>
#include <QStackedWidget>
#include "core/DiffEngine.h"
#include "core/GitManager.h"

class DiffSplitView;

#ifndef NO_QSCINTILLA
#include <Qsci/qsciscintilla.h>
#else
#include <QPlainTextEdit>
#endif

struct FileTab {
    QString filePath;
    bool dirty = false;
    bool inDiffMode = false;
#ifndef NO_QSCINTILLA
    QsciScintilla *editor = nullptr;
#else
    QPlainTextEdit *editor = nullptr;
#endif
    QStackedWidget *stack = nullptr;
    DiffSplitView *diffView = nullptr;
};

class CodeViewer : public QWidget {
    Q_OBJECT
public:
    explicit CodeViewer(QWidget *parent = nullptr);

    void loadFile(const QString &filePath);
    void refreshFile(const QString &filePath);
    void showDiff(const FileDiff &diff);
    void clearDiffMarkers();
    void scrollToLine(int line);

    bool saveCurrentFile();
    bool saveFile(int tabIndex);
    int saveAllFiles();
    bool isCurrentDirty() const;
    bool hasDirtyTabs() const;

    QString currentFile() const;

    void undo();
    void redo();
    void cut();
    void copy();
    void paste();

    void setGitManager(GitManager *mgr) { m_gitManager = mgr; }
    void showSplitDiff(const QString &filePath, const QString &oldContent,
                       const QString &newContent, const QString &leftLabel,
                       const QString &rightLabel);
    void toggleDiffMode();
    bool isInDiffMode() const;

signals:
    void fileSaved(const QString &filePath);
    void dirtyStateChanged(const QString &filePath, bool dirty);

private:
#ifndef NO_QSCINTILLA
    QsciScintilla *createEditor();
    void setLexerForFile(const QString &filePath, QsciScintilla *editor);
#else
    QPlainTextEdit *createEditor();
#endif
    void applyDiffMarkers(const FileDiff &diff);
    FileTab *currentTab();
    FileTab *tabForFile(const QString &filePath);
    int indexForFile(const QString &filePath);
    void updateTabTitle(int index);
    bool confirmCloseTab(int index);
    void onExternalFileChanged(const QString &filePath);
    void watchFile(const QString &filePath);
    void unwatchFile(const QString &filePath);
    void connectEditorSignals(FileTab &tab);

    void updateEmptyState();

    QTabWidget *m_tabWidget;
    QPushButton *m_diffToggleBtn;
    QWidget *m_emptyState = nullptr;
    QMap<int, FileTab> m_tabs;
    QFileSystemWatcher *m_fileWatcher;
    QSet<QString> m_savingFiles;
    GitManager *m_gitManager = nullptr;
};
