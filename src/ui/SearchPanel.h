#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QTreeWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QProcess>
#include <QDir>

class SearchPanel : public QWidget {
    Q_OBJECT
public:
    explicit SearchPanel(QWidget *parent = nullptr);

    void setRootPath(const QString &path);

signals:
    void fileSelected(const QString &filePath, int line);

private slots:
    void onSearch();
    void onResultClicked(QTreeWidgetItem *item, int column);

private:
    enum class SearchMode { FileName, TextContent };

    void searchFileNames(const QString &query);
    void searchTextContent(const QString &query);
    void addFileResults(const QString &rootPath, const QString &query, const QDir &dir);
    void applyThemeColors();

    QComboBox *m_modeCombo = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QCheckBox *m_regexCheck = nullptr;
    QTreeWidget *m_results = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_searchBtn = nullptr;
    QString m_rootPath;
    QProcess *m_grepProcess = nullptr;
    int m_resultCount = 0;
    static constexpr int MAX_RESULTS = 500;
};
