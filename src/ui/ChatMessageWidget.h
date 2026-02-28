#pragma once

#include <QFrame>
#include <QLabel>
#include <QTextBrowser>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class ChatMessageWidget : public QFrame {
    Q_OBJECT
public:
    enum Role { User, Assistant, Tool };

    explicit ChatMessageWidget(Role role, const QString &content,
                               QWidget *parent = nullptr);

    void appendContent(const QString &text);
    void appendHtmlOnly(const QString &html, const QString &plainTextForStorage);
    void setToolInfo(const QString &toolName, const QString &summary);
    void setTurnId(int turnId) { m_turnId = turnId; }
    int turnId() const { return m_turnId; }
    void showRevertButton(bool show);
    void showAcceptRejectButtons(bool show);
    void setReverted(bool reverted);
    QString rawContent() const { return m_rawContent; }

signals:
    void revertRequested(int turnId);
    void acceptRequested(int turnId);
    void rejectRequested(int turnId);
    void fileNavigationRequested(const QString &filePath, int line);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyStyle();
    void applyThemeColors();
    void setupAssistantContent(const QString &content);
    void setupUserContent(const QString &content);
    void setupToolWidget(const QString &toolName, const QString &summary);
    void resizeBrowser();

    Role m_role;
    int m_turnId = 0;
    QLabel *m_roleLabel = nullptr;
    QLabel *m_userLabel = nullptr;
    QTextBrowser *m_contentBrowser = nullptr;
    QWidget *m_toolDetailWidget = nullptr;
    QPushButton *m_revertBtn = nullptr;
    QPushButton *m_acceptBtn = nullptr;
    QPushButton *m_rejectBtn = nullptr;
    QPushButton *m_expandBtn = nullptr;
    QVBoxLayout *m_layout;
    QString m_rawContent;
    bool m_isCollapsed = true;
    bool m_resizePending = false;
};
