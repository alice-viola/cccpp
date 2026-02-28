#pragma once

#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QPropertyAnimation>
#include <QMap>
#include <QList>

struct ToolCallInfo {
    QString toolName;
    QString filePath;
    QString summary;
    QString oldString;
    QString newString;
    bool isEdit = false;
};

class ToolCallGroupWidget : public QFrame {
    Q_OBJECT
public:
    explicit ToolCallGroupWidget(QWidget *parent = nullptr);

    void addToolCall(const ToolCallInfo &info);
    void finalize();
    int toolCount() const { return m_calls.size(); }

private:
    void updateSummaryLabel();
    void rebuildDetailView();
    void applyThemeColors();
    QWidget *createDiffView(const QString &oldStr, const QString &newStr);

    QVBoxLayout *m_layout;
    QHBoxLayout *m_headerLayout;
    QPushButton *m_expandBtn;
    QLabel *m_summaryLabel;
    QWidget *m_detailContainer;
    QVBoxLayout *m_detailLayout;
    QPropertyAnimation *m_expandAnim = nullptr;
    bool m_expanded = false;

    QList<ToolCallInfo> m_calls;
    QMap<QString, int> m_toolCounts;
};
