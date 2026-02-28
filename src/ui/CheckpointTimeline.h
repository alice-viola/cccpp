#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QList>

class Database;

struct CheckpointEntry {
    int turnId = 0;
    QString sessionId;
    QString checkpointUuid;
    qint64 timestamp = 0;
    QStringList filesChanged;
    QString summary;
};

class CheckpointTimeline : public QWidget {
    Q_OBJECT
public:
    explicit CheckpointTimeline(QWidget *parent = nullptr);

    void setDatabase(Database *db);
    void setSessionId(const QString &id);
    void refresh();

signals:
    void restoreRequested(const QString &checkpointUuid);

private:
    void rebuild();
    void applyThemeColors();

    QVBoxLayout *m_layout;
    QScrollArea *m_scrollArea;
    QVBoxLayout *m_entriesLayout;
    QWidget *m_entriesWidget;
    QLabel *m_titleLabel;
    QPushButton *m_refreshBtn;

    Database *m_database = nullptr;
    QString m_sessionId;
    QList<CheckpointEntry> m_entries;
};
