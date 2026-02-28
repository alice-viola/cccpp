#include "core/DiffEngine.h"
#include <QFile>
#include <algorithm>

DiffEngine::DiffEngine(QObject *parent)
    : QObject(parent)
{
}

FileDiff DiffEngine::computeDiff(const QString &oldContent, const QString &newContent,
                                  const QString &filePath)
{
    FileDiff diff;
    diff.filePath = filePath;

    if (oldContent.isEmpty() && !newContent.isEmpty()) {
        diff.isNewFile = true;
        DiffHunk hunk;
        hunk.type = DiffHunk::Added;
        hunk.startLine = 0;
        hunk.lines = newContent.split('\n');
        hunk.count = hunk.lines.size();
        diff.hunks.append(hunk);
        return diff;
    }

    if (!oldContent.isEmpty() && newContent.isEmpty()) {
        diff.isDeleted = true;
        return diff;
    }

    QStringList oldLines = oldContent.split('\n');
    QStringList newLines = newContent.split('\n');

    int m = oldLines.size();
    int n = newLines.size();

    // LCS-based diff for reasonable sizes, simple fallback otherwise
    if (static_cast<long long>(m) * n > 25000000) {
        int maxLines = qMax(m, n);
        for (int i = 0; i < maxLines; ++i) {
            QString oldLine = (i < m) ? oldLines[i] : QString();
            QString newLine = (i < n) ? newLines[i] : QString();
            if (oldLine != newLine) {
                if (i < m) {
                    DiffHunk removed;
                    removed.type = DiffHunk::Removed;
                    removed.startLine = i;
                    removed.count = 1;
                    removed.lines << oldLine;
                    diff.hunks.append(removed);
                }
                if (i < n) {
                    DiffHunk added;
                    added.type = DiffHunk::Added;
                    added.startLine = i;
                    added.count = 1;
                    added.lines << newLine;
                    diff.hunks.append(added);
                }
            }
        }
        return diff;
    }

    QVector<QVector<int>> dp(m + 1, QVector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            if (oldLines[i - 1] == newLines[j - 1])
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = qMax(dp[i - 1][j], dp[i][j - 1]);
        }
    }

    int i = m, j = n;
    struct DiffLine {
        DiffHunk::Type type;
        int lineNum;
        QString text;
    };
    QList<DiffLine> diffLines;

    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && oldLines[i - 1] == newLines[j - 1]) {
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            diffLines.prepend({DiffHunk::Added, j - 1, newLines[j - 1]});
            --j;
        } else if (i > 0) {
            diffLines.prepend({DiffHunk::Removed, i - 1, oldLines[i - 1]});
            --i;
        }
    }

    for (int k = 0; k < diffLines.size(); ) {
        DiffHunk hunk;
        hunk.type = diffLines[k].type;
        hunk.startLine = diffLines[k].lineNum;
        hunk.count = 0;
        while (k < diffLines.size() && diffLines[k].type == hunk.type) {
            hunk.lines.append(diffLines[k].text);
            hunk.count++;
            k++;
        }
        diff.hunks.append(hunk);
    }

    return diff;
}

void DiffEngine::recordEditToolChange(const QString &filePath,
                                       const QString &oldString, const QString &newString)
{
    // Build diff directly from old_string/new_string — no disk read needed.
    // The file on disk is already modified by claude, so we can't read the "before" state.
    FileDiff editDiff = computeDiff(oldString, newString, filePath);

    // Track which lines in the file were affected.
    // We need to find where old_string appears in the file to get absolute line numbers.
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        QString content = QString::fromUtf8(file.readAll());
        file.close();

        // Find where the new_string is in the file now (it was just inserted)
        int pos = content.indexOf(newString);
        if (pos >= 0) {
            int startLine = content.left(pos).count('\n');
            // Offset all hunk line numbers by the start position
            for (auto &hunk : editDiff.hunks)
                hunk.startLine += startLine;
        }
    }

    // Accumulate diffs per file
    if (m_fileDiffs.contains(filePath)) {
        auto &existing = m_fileDiffs[filePath];
        existing.hunks.append(editDiff.hunks);
    } else {
        m_fileDiffs[filePath] = editDiff;
    }

    m_pendingDiffs.append(editDiff);
    emit fileChanged(filePath, m_fileDiffs[filePath]);
}

void DiffEngine::recordWriteToolChange(const QString &filePath, const QString &newContent)
{
    FileDiff diff;
    diff.filePath = filePath;
    diff.isNewFile = true;

    DiffHunk hunk;
    hunk.type = DiffHunk::Added;
    hunk.startLine = 0;
    hunk.lines = newContent.split('\n');
    hunk.count = hunk.lines.size();
    diff.hunks.append(hunk);

    m_fileDiffs[filePath] = diff;
    m_pendingDiffs.append(diff);
    emit fileChanged(filePath, diff);
}

FileDiff DiffEngine::diffForFile(const QString &filePath) const
{
    return m_fileDiffs.value(filePath);
}

void DiffEngine::clearPendingDiffs()
{
    m_pendingDiffs.clear();
    m_fileDiffs.clear();
}
