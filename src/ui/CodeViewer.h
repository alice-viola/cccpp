#pragma once

#include <QWidget>
#include <QLabel>
#include <QTabWidget>
#include <QMap>
#include <QSet>
#include <QFileSystemWatcher>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include "core/DiffEngine.h"
#include "core/GitManager.h"
#include "ui/ThemeManager.h"

class DiffSplitView;
class InlineDiffOverlay;
class InlineEditBar;
class BreadcrumbBar;
class FindBar;

#ifndef NO_QSCINTILLA
#include <Qsci/qsciscintilla.h>
#else
#include <QPlainTextEdit>
#endif

struct InlineHunkState {
    int startLine = 0;
    int addedLineCount = 0;
    int annotationLine = -1; // line where annotation is attached (line before edit)
    QString oldText;
    QString newText;
    bool resolved = false;
};

struct FileTab {
    QString filePath;
    bool dirty = false;
    bool inDiffMode = false;
    bool isMarkdown = false;
    bool markdownShowRaw = false;
#ifndef NO_QSCINTILLA
    QsciScintilla *editor = nullptr;
#else
    QPlainTextEdit *editor = nullptr;
#endif
    QStackedWidget *stack = nullptr;
    DiffSplitView *diffView = nullptr;
    QTextBrowser *markdownView = nullptr;
    QList<InlineHunkState> inlineHunks;

    // Streaming edit state (Feature 2)
    bool streamingEdit = false;
    int streamEditStartLine = -1;
    int streamEditInsertLine = -1;
    QString streamOldText;
    QString streamAccumulated;
};

class CodeViewer : public QWidget {
    Q_OBJECT
public:
    explicit CodeViewer(QWidget *parent = nullptr);

    void loadFile(const QString &filePath);
    void closeFile(const QString &filePath);
    void openMarkdown(const QString &filePath);
    void refreshFile(const QString &filePath);
    void forceReloadFile(const QString &filePath);
    void showDiff(const FileDiff &diff);
    void clearDiffMarkers();
    void clearAllDiffMarkers();
    void scrollToLine(int line);

    bool saveCurrentFile();
    bool saveFile(int tabIndex);
    int saveAllFiles();
    bool isCurrentDirty() const;
    bool hasDirtyTabs() const;

    QString currentFile() const;
    void setRootPath(const QString &path) { m_rootPath = path; }
    QStringList openFiles() const;
    QString selectedText() const;
    int currentLine() const;

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
    void toggleMarkdownRaw();
    bool isInDiffMode() const;

    // Inline diff overlay for AI edits (legacy)
    void showInlineDiffOverlay(const QString &filePath, const QString &oldText,
                               const QString &newText, int startLine);
    void hideInlineDiffOverlay();

    // Cursor-style inline diff in the editor
    void showInlineDiff(const QString &filePath, const QString &oldText,
                        const QString &newText, int startLine);
    void acceptInlineHunk(const QString &filePath, int hunkIndex);
    void rejectInlineHunk(const QString &filePath, int hunkIndex);
    void acceptAllInlineHunks(const QString &filePath);
    void rejectAllInlineHunks(const QString &filePath);
    void clearInlineDiffs(const QString &filePath);

    // Streaming edit visualization (Feature 2)
    void beginStreamingEdit(const QString &filePath, const QString &oldText, int startLine);
    void appendStreamingContent(const QString &filePath, const QString &delta);
    void finalizeStreamingEdit(const QString &filePath);

    // Close all open file tabs
    void closeAllFiles();

    // Find bar
    void showFindBar();
    void hideFindBar();

    // Inline edit (Cmd+K)
    void showInlineEditBar();
    void hideInlineEditBar();

signals:
    void fileSaved(const QString &filePath);
    void dirtyStateChanged(const QString &filePath, bool dirty);
    void inlineEditSubmitted(const QString &filePath, const QString &selectedCode,
                             const QString &instruction);
    void inlineDiffAccepted(const QString &filePath);
    void inlineDiffRejected(const QString &filePath, const QString &oldText,
                            const QString &newText);
protected:
    void resizeEvent(QResizeEvent *event) override;

private:
#ifndef NO_QSCINTILLA
    QsciScintilla *createEditor();
    void setLexerForFile(const QString &filePath, QsciScintilla *editor);
    void applyEditorThemeColors(QsciScintilla *editor);
#else
    QPlainTextEdit *createEditor();
#endif
    void applyThemeColors();
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
#ifndef NO_QSCINTILLA
    void setupInlineDiffMargins(QsciScintilla *ed);
    void renderInlineHunk(FileTab &tab, int hunkIndex);
    void clearInlineHunkVisuals(FileTab &tab, int hunkIndex);
    int findLineOfText(QsciScintilla *ed, const QString &text);
    void onDiffMarginClicked(int margin, int line, Qt::KeyboardModifiers mods);
#endif

    QTabWidget *m_tabWidget;
    QPushButton *m_diffToggleBtn;
    QPushButton *m_closeAllBtn;
    QWidget *m_emptyState = nullptr;
    FindBar *m_findBar = nullptr;
    BreadcrumbBar *m_breadcrumb = nullptr;
    QMap<int, FileTab> m_tabs;
    QFileSystemWatcher *m_fileWatcher;
    QSet<QString> m_savingFiles;
    GitManager *m_gitManager = nullptr;
    QString m_rootPath;

    InlineDiffOverlay *m_inlineDiffOverlay = nullptr;
    InlineEditBar *m_inlineEditBar = nullptr;
};
