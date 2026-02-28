#pragma once

#include <QFrame>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QFileSystemModel>
#include <QStringList>

struct ContextItem {
    enum Type { File, OpenTab, RecentFile, Folder };
    Type type;
    QString displayName;
    QString fullPath;
    QString icon;
};

class ContextPopup : public QFrame {
    Q_OBJECT
public:
    explicit ContextPopup(QWidget *parent = nullptr);

    void setWorkspacePath(const QString &path);
    void setOpenFiles(const QStringList &files);
    void setRecentFiles(const QStringList &files);

    void updateFilter(const QString &filter);
    void selectNext();
    void selectPrevious();
    QString acceptSelection();

    bool hasSelection() const;
    int itemCount() const;

signals:
    void itemSelected(const QString &displayToken, const QString &fullPath);
    void dismissed();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuild(const QString &filter);
    void applyThemeColors();

    QVBoxLayout *m_layout;
    QListWidget *m_list;
    QString m_workspacePath;
    QStringList m_openFiles;
    QStringList m_recentFiles;
    QList<ContextItem> m_items;
};
