#pragma once

#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTextBrowser>
#include <QList>

struct DiffHunkData {
    int startLine = 0;
    int oldLineCount = 0;
    int newLineCount = 0;
    QString oldText;
    QString newText;
    QString filePath;
    bool accepted = false;
    bool rejected = false;
};

class InlineDiffOverlay : public QFrame {
    Q_OBJECT
public:
    explicit InlineDiffOverlay(QWidget *parent = nullptr);

    void setFilePath(const QString &path);
    void setDiff(const QString &oldText, const QString &newText, int startLine = 0);
    void clear();

    bool hasChanges() const { return !m_hunks.isEmpty(); }

signals:
    void acceptAll();
    void rejectAll();
    void acceptHunk(int index);
    void rejectHunk(int index);
    void closed();

private:
    void rebuild();
    void applyThemeColors();

    QVBoxLayout *m_layout;
    QHBoxLayout *m_headerLayout;
    QLabel *m_titleLabel;
    QPushButton *m_acceptAllBtn;
    QPushButton *m_rejectAllBtn;
    QPushButton *m_closeBtn;
    QWidget *m_diffContent;
    QVBoxLayout *m_diffLayout;
    QString m_filePath;
    QList<DiffHunkData> m_hunks;
};
