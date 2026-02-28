#pragma once

#include <QWidget>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QList>
#include "core/DiffEngine.h"
#include "core/GitManager.h"

#ifndef NO_QSCINTILLA
#include <Qsci/qsciscintilla.h>
#else
#include <QPlainTextEdit>
#endif

class DiffSplitView : public QWidget {
    Q_OBJECT
public:
    explicit DiffSplitView(QWidget *parent = nullptr);

    void showDiff(const QString &filePath, const QString &oldContent, const QString &newContent,
                  const QString &leftLabel = "HEAD", const QString &rightLabel = "Working Tree");
    void showBinaryPlaceholder(const QString &filePath);
    void clear();

    QString currentFile() const { return m_filePath; }

    // Navigate between hunks
    void nextHunk();
    void prevHunk();

signals:
    void closed();

private:
    struct AlignedLine {
        enum Type { Context, Added, Removed, Phantom };
        Type type = Context;
        QString text;
        int originalLine = -1; // -1 for phantom lines
    };

    void setupUI();
    void buildAlignedLines(const QString &oldContent, const QString &newContent);
    void populateEditors();
    void applyMarkers();
    void syncScroll(int value, bool fromLeft);

#ifndef NO_QSCINTILLA
    QsciScintilla *createDiffEditor();
#else
    QPlainTextEdit *createDiffEditor();
#endif

    QLabel *m_leftHeader;
    QLabel *m_rightHeader;
    QPushButton *m_closeBtn;
    QPushButton *m_prevHunkBtn;
    QPushButton *m_nextHunkBtn;
    QSplitter *m_splitter;

#ifndef NO_QSCINTILLA
    QsciScintilla *m_leftEditor = nullptr;
    QsciScintilla *m_rightEditor = nullptr;
#else
    QPlainTextEdit *m_leftEditor = nullptr;
    QPlainTextEdit *m_rightEditor = nullptr;
#endif

    QWidget *m_binaryPlaceholder;

    QString m_filePath;
    QList<AlignedLine> m_leftLines;
    QList<AlignedLine> m_rightLines;
    QList<int> m_hunkStartLines; // line indices in the aligned view where hunks start
    int m_currentHunkIdx = -1;
    bool m_syncingScroll = false;
};
