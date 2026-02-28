#pragma once

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QStringList>

class BreadcrumbBar : public QWidget {
    Q_OBJECT
public:
    explicit BreadcrumbBar(QWidget *parent = nullptr);

    void setFilePath(const QString &filePath, const QString &rootPath);
    void clear();

signals:
    void segmentClicked(const QString &path);

private:
    void rebuild();
    void applyThemeColors();

    QHBoxLayout *m_layout;
    QString m_filePath;
    QString m_rootPath;
    QList<QWidget *> m_segments;
};
