#include "ui/DiffSplitView.h"
#include "ui/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QFont>

#ifndef NO_QSCINTILLA
#include <Qsci/qscilexer.h>
#endif

// Marker IDs for background coloring
static constexpr int MARKER_ADDED   = 3;
static constexpr int MARKER_REMOVED = 4;
static constexpr int MARKER_PHANTOM = 5;

DiffSplitView::DiffSplitView(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void DiffSplitView::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_headerBar = new QWidget(this);
    m_headerBar->setFixedHeight(26);

    auto *headerLayout = new QHBoxLayout(m_headerBar);
    headerLayout->setContentsMargins(8, 0, 4, 0);
    headerLayout->setSpacing(6);

    m_leftHeader = new QLabel(this);
    headerLayout->addWidget(m_leftHeader);

    m_arrowLabel = new QLabel("\xe2\x86\x92", this);
    headerLayout->addWidget(m_arrowLabel);

    m_rightHeader = new QLabel(this);
    headerLayout->addWidget(m_rightHeader);

    headerLayout->addStretch();

    m_prevHunkBtn = new QPushButton("\xe2\x86\x91", this);
    m_prevHunkBtn->setToolTip("Previous Hunk");
    m_prevHunkBtn->setFixedSize(24, 20);
    headerLayout->addWidget(m_prevHunkBtn);
    connect(m_prevHunkBtn, &QPushButton::clicked, this, &DiffSplitView::prevHunk);

    m_nextHunkBtn = new QPushButton("\xe2\x86\x93", this);
    m_nextHunkBtn->setToolTip("Next Hunk");
    m_nextHunkBtn->setFixedSize(24, 20);
    headerLayout->addWidget(m_nextHunkBtn);
    connect(m_nextHunkBtn, &QPushButton::clicked, this, &DiffSplitView::nextHunk);

    m_closeBtn = new QPushButton("\xc3\x97", this);
    m_closeBtn->setToolTip("Close Diff View");
    m_closeBtn->setFixedSize(24, 20);
    headerLayout->addWidget(m_closeBtn);
    connect(m_closeBtn, &QPushButton::clicked, this, [this] {
        clear();
        emit closed();
    });

    mainLayout->addWidget(m_headerBar);

    m_binaryPlaceholder = new QWidget(this);
    auto *bpLayout = new QVBoxLayout(m_binaryPlaceholder);
    bpLayout->setAlignment(Qt::AlignCenter);
    m_bpLabel = new QLabel("Binary file differs", m_binaryPlaceholder);
    m_bpLabel->setAlignment(Qt::AlignCenter);
    bpLayout->addWidget(m_bpLabel);
    m_binaryPlaceholder->hide();
    mainLayout->addWidget(m_binaryPlaceholder);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);

    m_leftEditor = createDiffEditor();
    m_rightEditor = createDiffEditor();

    m_splitter->addWidget(m_leftEditor);
    m_splitter->addWidget(m_rightEditor);
    m_splitter->setSizes({1, 1});

    mainLayout->addWidget(m_splitter, 1);

#ifndef NO_QSCINTILLA
    connect(m_leftEditor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) { syncScroll(val, true); });
    connect(m_rightEditor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) { syncScroll(val, false); });
#else
    connect(m_leftEditor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) { syncScroll(val, true); });
    connect(m_rightEditor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) { syncScroll(val, false); });
#endif

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &DiffSplitView::applyThemeColors);
}

void DiffSplitView::applyThemeColors()
{
    const auto &pal = ThemeManager::instance().palette();

    m_headerBar->setStyleSheet(QStringLiteral("QWidget { background: %1; border-bottom: 1px solid %2; }")
        .arg(pal.bg_base.name(), pal.border_standard.name()));

    m_leftHeader->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; background: transparent; }")
        .arg(pal.red.name()));
    m_arrowLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; background: transparent; }")
        .arg(pal.text_muted.name()));
    m_rightHeader->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; background: transparent; }")
        .arg(pal.green.name()));

    auto btnStyle = QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; font-size: 12px; padding: 2px 6px; }"
        "QPushButton:hover { color: %2; background: %3; border-radius: 4px; }")
        .arg(pal.text_muted.name(), pal.text_primary.name(), pal.bg_raised.name());
    m_prevHunkBtn->setStyleSheet(btnStyle);
    m_nextHunkBtn->setStyleSheet(btnStyle);
    m_closeBtn->setStyleSheet(btnStyle);

    m_bpLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(pal.text_muted.name()));

#ifndef NO_QSCINTILLA
    for (auto *ed : {m_leftEditor, m_rightEditor}) {
        ed->setMarginsForegroundColor(pal.overlay0);
        ed->setMarginsBackgroundColor(pal.bg_base);
        ed->setCaretForegroundColor(pal.text_primary);
        ed->setPaper(pal.bg_window);
        ed->setColor(pal.text_primary);
        ed->setSelectionBackgroundColor(pal.pressed_raised);
        ed->setSelectionForegroundColor(pal.text_primary);
        ed->setMarkerBackgroundColor(pal.diff_add_bg, MARKER_ADDED);
        ed->setMarkerBackgroundColor(pal.diff_del_bg, MARKER_REMOVED);
        ed->setMarkerBackgroundColor(pal.diff_phantom_bg, MARKER_PHANTOM);
    }
#else
    auto edStyle = QStringLiteral(
        "QPlainTextEdit { background: %1; color: %2; border: none; "
        "font-family: Menlo, monospace; font-size: 13px; }")
        .arg(pal.bg_window.name(), pal.text_primary.name());
    m_leftEditor->setStyleSheet(edStyle);
    m_rightEditor->setStyleSheet(edStyle);
#endif
}

// ---------------------------------------------------------------------------
// Editor factory
// ---------------------------------------------------------------------------

#ifndef NO_QSCINTILLA

QsciScintilla *DiffSplitView::createDiffEditor()
{
    const auto &pal = ThemeManager::instance().palette();

    auto *ed = new QsciScintilla(this);
    ed->setReadOnly(true);
    ed->setMarginType(0, QsciScintilla::NumberMargin);
    ed->setMarginWidth(0, "00000");
    ed->setMarginsForegroundColor(pal.overlay0);
    ed->setMarginsBackgroundColor(pal.bg_base);
    ed->setMarginsFont(QFont("Menlo", 12));
    ed->setCaretForegroundColor(pal.text_primary);
    ed->setCaretLineVisible(false);
    ed->setPaper(pal.bg_window);
    ed->setColor(pal.text_primary);
    ed->setFont(QFont("Menlo", 13));
    ed->setTabWidth(4);
    ed->setFolding(QsciScintilla::NoFoldStyle);
    ed->setEolMode(QsciScintilla::EolUnix);
    ed->setUtf8(true);
    ed->setWrapMode(QsciScintilla::WrapNone);
    ed->setSelectionBackgroundColor(pal.pressed_raised);
    ed->setSelectionForegroundColor(pal.text_primary);

    // Define background markers — unified palette
    ed->markerDefine(QsciScintilla::Background, MARKER_ADDED);
    ed->setMarkerBackgroundColor(pal.diff_add_bg, MARKER_ADDED);

    ed->markerDefine(QsciScintilla::Background, MARKER_REMOVED);
    ed->setMarkerBackgroundColor(pal.diff_del_bg, MARKER_REMOVED);

    ed->markerDefine(QsciScintilla::Background, MARKER_PHANTOM);
    ed->setMarkerBackgroundColor(pal.diff_phantom_bg, MARKER_PHANTOM);

    return ed;
}

#else

QPlainTextEdit *DiffSplitView::createDiffEditor()
{
    auto *ed = new QPlainTextEdit(this);
    ed->setReadOnly(true);
    ed->setTabStopDistance(32);
    ed->setLineWrapMode(QPlainTextEdit::NoWrap);
    ed->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: %1; color: %2; border: none; "
        "font-family: Menlo, monospace; font-size: 13px; }")
        .arg(ThemeManager::instance().palette().bg_window.name(),
             ThemeManager::instance().palette().text_primary.name()));
    return ed;
}

#endif

// ---------------------------------------------------------------------------
// Show diff
// ---------------------------------------------------------------------------

void DiffSplitView::showDiff(const QString &filePath, const QString &oldContent, const QString &newContent,
                              const QString &leftLabel, const QString &rightLabel)
{
    m_filePath = filePath;
    m_binaryPlaceholder->hide();
    m_splitter->show();

    m_leftHeader->setText(QStringLiteral("a/%1 (%2)").arg(filePath, leftLabel));
    m_rightHeader->setText(QStringLiteral("b/%1 (%2)").arg(filePath, rightLabel));

    buildAlignedLines(oldContent, newContent);
    populateEditors();
    applyMarkers();
}

void DiffSplitView::showBinaryPlaceholder(const QString &filePath)
{
    m_filePath = filePath;
    m_leftHeader->setText(QStringLiteral("a/%1").arg(filePath));
    m_rightHeader->setText(QStringLiteral("b/%1").arg(filePath));
    m_splitter->hide();
    m_binaryPlaceholder->show();
}

void DiffSplitView::clear()
{
    m_filePath.clear();
    m_leftLines.clear();
    m_rightLines.clear();
    m_hunkStartLines.clear();
    m_currentHunkIdx = -1;

#ifndef NO_QSCINTILLA
    m_leftEditor->setText("");
    m_rightEditor->setText("");
#else
    m_leftEditor->setPlainText("");
    m_rightEditor->setPlainText("");
#endif

    m_leftHeader->clear();
    m_rightHeader->clear();
    m_binaryPlaceholder->hide();
    m_splitter->show();
}

// ---------------------------------------------------------------------------
// Alignment algorithm: LCS-based with phantom lines
// ---------------------------------------------------------------------------

void DiffSplitView::buildAlignedLines(const QString &oldContent, const QString &newContent)
{
    m_leftLines.clear();
    m_rightLines.clear();
    m_hunkStartLines.clear();

    QStringList oldLines = oldContent.split('\n');
    QStringList newLines = newContent.split('\n');

    int m = oldLines.size();
    int n = newLines.size();

    // For very large files, use a simplified approach
    if (static_cast<long long>(m) * n > 25000000) {
        int maxLines = qMax(m, n);
        for (int i = 0; i < maxLines; ++i) {
            bool hasOld = (i < m);
            bool hasNew = (i < n);
            if (hasOld && hasNew && oldLines[i] == newLines[i]) {
                m_leftLines.append({AlignedLine::Context, oldLines[i], i});
                m_rightLines.append({AlignedLine::Context, newLines[i], i});
            } else {
                if (hasOld) {
                    m_leftLines.append({AlignedLine::Removed, oldLines[i], i});
                    m_rightLines.append({AlignedLine::Phantom, "", -1});
                }
                if (hasNew) {
                    m_leftLines.append({AlignedLine::Phantom, "", -1});
                    m_rightLines.append({AlignedLine::Added, newLines[i], i});
                }
            }
        }
        return;
    }

    // LCS via DP
    QVector<QVector<int>> dp(m + 1, QVector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            if (oldLines[i - 1] == newLines[j - 1])
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = qMax(dp[i - 1][j], dp[i][j - 1]);
        }
    }

    // Backtrack to build aligned view
    struct DiffOp { char type; int oldIdx; int newIdx; };
    QList<DiffOp> ops;

    int i = m, j = n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && oldLines[i - 1] == newLines[j - 1]) {
            ops.prepend({'=', i - 1, j - 1});
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            ops.prepend({'+', -1, j - 1});
            --j;
        } else if (i > 0) {
            ops.prepend({'-', i - 1, -1});
            --i;
        }
    }

    // Convert ops to aligned lines, grouping removals+additions
    bool inHunk = false;

    for (int k = 0; k < ops.size(); ++k) {
        const auto &op = ops[k];
        if (op.type == '=') {
            inHunk = false;
            m_leftLines.append({AlignedLine::Context, oldLines[op.oldIdx], op.oldIdx});
            m_rightLines.append({AlignedLine::Context, newLines[op.newIdx], op.newIdx});
        } else if (op.type == '-') {
            if (!inHunk) {
                m_hunkStartLines.append(m_leftLines.size());
                inHunk = true;
            }
            m_leftLines.append({AlignedLine::Removed, oldLines[op.oldIdx], op.oldIdx});
            m_rightLines.append({AlignedLine::Phantom, "", -1});
        } else if (op.type == '+') {
            if (!inHunk) {
                m_hunkStartLines.append(m_leftLines.size());
                inHunk = true;
            }
            m_leftLines.append({AlignedLine::Phantom, "", -1});
            m_rightLines.append({AlignedLine::Added, newLines[op.newIdx], op.newIdx});
        }
    }
}

// ---------------------------------------------------------------------------
// Populate editors with aligned text
// ---------------------------------------------------------------------------

void DiffSplitView::populateEditors()
{
    QStringList leftText, rightText;
    leftText.reserve(m_leftLines.size());
    rightText.reserve(m_rightLines.size());

    for (const auto &line : qAsConst(m_leftLines))
        leftText.append(line.text);
    for (const auto &line : qAsConst(m_rightLines))
        rightText.append(line.text);

#ifndef NO_QSCINTILLA
    m_leftEditor->setText(leftText.join('\n'));
    m_rightEditor->setText(rightText.join('\n'));
#else
    m_leftEditor->setPlainText(leftText.join('\n'));
    m_rightEditor->setPlainText(rightText.join('\n'));
#endif
}

// ---------------------------------------------------------------------------
// Apply color markers
// ---------------------------------------------------------------------------

void DiffSplitView::applyMarkers()
{
#ifndef NO_QSCINTILLA
    m_leftEditor->markerDeleteAll(MARKER_ADDED);
    m_leftEditor->markerDeleteAll(MARKER_REMOVED);
    m_leftEditor->markerDeleteAll(MARKER_PHANTOM);
    m_rightEditor->markerDeleteAll(MARKER_ADDED);
    m_rightEditor->markerDeleteAll(MARKER_REMOVED);
    m_rightEditor->markerDeleteAll(MARKER_PHANTOM);

    for (int i = 0; i < m_leftLines.size(); ++i) {
        switch (m_leftLines[i].type) {
        case AlignedLine::Removed:
            m_leftEditor->markerAdd(i, MARKER_REMOVED);
            break;
        case AlignedLine::Phantom:
            m_leftEditor->markerAdd(i, MARKER_PHANTOM);
            break;
        default:
            break;
        }
    }

    for (int i = 0; i < m_rightLines.size(); ++i) {
        switch (m_rightLines[i].type) {
        case AlignedLine::Added:
            m_rightEditor->markerAdd(i, MARKER_ADDED);
            break;
        case AlignedLine::Phantom:
            m_rightEditor->markerAdd(i, MARKER_PHANTOM);
            break;
        default:
            break;
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Synchronized scrolling
// ---------------------------------------------------------------------------

void DiffSplitView::syncScroll(int value, bool fromLeft)
{
    if (m_syncingScroll) return;
    m_syncingScroll = true;

    if (fromLeft) {
#ifndef NO_QSCINTILLA
        m_rightEditor->verticalScrollBar()->setValue(value);
#else
        m_rightEditor->verticalScrollBar()->setValue(value);
#endif
    } else {
#ifndef NO_QSCINTILLA
        m_leftEditor->verticalScrollBar()->setValue(value);
#else
        m_leftEditor->verticalScrollBar()->setValue(value);
#endif
    }

    m_syncingScroll = false;
}

// ---------------------------------------------------------------------------
// Hunk navigation
// ---------------------------------------------------------------------------

void DiffSplitView::nextHunk()
{
    if (m_hunkStartLines.isEmpty()) return;

    m_currentHunkIdx++;
    if (m_currentHunkIdx >= m_hunkStartLines.size())
        m_currentHunkIdx = 0;

    int line = m_hunkStartLines[m_currentHunkIdx];
#ifndef NO_QSCINTILLA
    m_leftEditor->setCursorPosition(line, 0);
    m_leftEditor->ensureLineVisible(line);
#else
    auto cursor = m_leftEditor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line);
    m_leftEditor->setTextCursor(cursor);
    m_leftEditor->ensureCursorVisible();
#endif
}

void DiffSplitView::prevHunk()
{
    if (m_hunkStartLines.isEmpty()) return;

    m_currentHunkIdx--;
    if (m_currentHunkIdx < 0)
        m_currentHunkIdx = m_hunkStartLines.size() - 1;

    int line = m_hunkStartLines[m_currentHunkIdx];
#ifndef NO_QSCINTILLA
    m_leftEditor->setCursorPosition(line, 0);
    m_leftEditor->ensureLineVisible(line);
#else
    auto cursor = m_leftEditor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line);
    m_leftEditor->setTextCursor(cursor);
    m_leftEditor->ensureCursorVisible();
#endif
}
