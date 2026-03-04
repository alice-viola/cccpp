#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QMap>

struct FileChange {
    QString filePath;
    enum Type { Modified, Created, Deleted };
    Type type = Modified;
    int linesAdded = 0;
    int linesRemoved = 0;
    QString sessionId;
    int turnId = 0;
};

class FileChangeItem : public QWidget {
    Q_OBJECT
public:
    explicit FileChangeItem(const FileChange &change, const QString &rootPath,
                            QWidget *parent = nullptr);
    void update(const FileChange &change);
    QString filePath() const { return m_filePath; }

signals:
    void clicked(const QString &filePath);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;

private:
    QString m_filePath;
    QString m_relativePath;
    FileChange::Type m_type = FileChange::Modified;
    int m_linesAdded = 0;
    int m_linesRemoved = 0;
    bool m_hovered = false;
};

class EffectsPanel : public QWidget {
    Q_OBJECT
public:
    explicit EffectsPanel(QWidget *parent = nullptr);

    void setRootPath(const QString &rootPath);
    void setCurrentSession(const QString &sessionId);
    void setCurrentTurnId(int turnId);
    void setHighlightedTurn(int turnId);
    void setTurnTimestamps(const QMap<int, qint64> &timestamps);
    void onFileChanged(const QString &filePath, const FileChange &change);
    void populateFromHistory(const QString &sessionId, const QList<FileChange> &changes);
    bool hasChangesForSession(const QString &sessionId) const;
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void fileClicked(const QString &filePath);
    void turnClicked(int turnId);

private:
    void applyThemeColors();
    void rebuildList();
    void updateStats();
    void updateTurnDividerLabels();
    QString formatRelativeTime(qint64 refTimestamp, qint64 turnTimestamp) const;

    QVBoxLayout *m_mainLayout = nullptr;
    QWidget *m_header = nullptr;
    QLabel *m_headerLabel = nullptr;
    QPushButton *m_scopeToggle = nullptr;
    QLabel *m_statsLabel = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_fileLayout = nullptr;

    QString m_rootPath;
    QString m_currentSessionId;
    int m_currentTurnId = 0;
    int m_highlightedTurn = -1;
    bool m_showAllSessions = false;

    // sessionId -> turnId -> filePath -> FileChange
    QMap<QString, QMap<int, QMap<QString, FileChange>>> m_sessionChanges;
    QMap<int, qint64> m_turnTimestamps;  // turnId -> epoch secs
    QMap<QString, FileChangeItem *> m_items;  // compound key -> item
    QMap<int, QLabel *> m_turnDividers;  // turnId -> divider label
};
