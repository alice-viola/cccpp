#pragma once

#include <QFrame>
#include <QLabel>
#include <QTextBrowser>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStringList>
#include <QDateTime>

class ChatMessageWidget : public QFrame {
    Q_OBJECT
public:
    enum Role { User, Assistant, Tool };

    explicit ChatMessageWidget(Role role, const QString &content,
                               QWidget *parent = nullptr);

    void appendContent(const QString &text);
    void appendContentFast(const QString &text);
    void syncMarkdown();
    void finalizeContent();
    bool needsResize() const { return m_needsResize; }
    void flushDeferredResize() { if (m_needsResize) resizeBrowser(); }
    void appendRawHtml(const QString &html, const QString &plainSummary);
    void appendHtmlOnly(const QString &html, const QString &plainTextForStorage);
    void setToolInfo(const QString &toolName, const QString &summary);
    void setTurnId(int turnId) { m_turnId = turnId; }
    int turnId() const { return m_turnId; }
    void setImages(const QList<QByteArray> &imageDataList);
    void showRevertButton(bool show);
    void showAcceptRejectButtons(bool show);
    void setReverted(bool reverted);
    void setHeaderVisible(bool visible) { if (m_headerWidget) m_headerWidget->setVisible(visible); }
    void setTimestamp(const QDateTime &dt);
    QString rawContent() const { return m_rawContent; }

signals:
    void revertRequested(int turnId);
    void acceptRequested(int turnId);
    void rejectRequested(int turnId);
    void fileNavigationRequested(const QString &filePath, int line);
    void applyCodeRequested(const QString &code, const QString &language);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void applyStyle();
    void applyThemeColors();
    void setupAssistantContent(const QString &content);
    void setupToolWidget(const QString &toolName, const QString &summary);
    void resizeBrowser();
    bool isInViewport() const;

    Role m_role;
    int m_turnId = 0;
    QLabel *m_roleLabel = nullptr;
    QLabel *m_userLabel = nullptr;
    QTextBrowser *m_contentBrowser = nullptr;
    QWidget *m_headerWidget = nullptr;
    QWidget *m_toolDetailWidget = nullptr;
    QPushButton *m_revertBtn = nullptr;
    QPushButton *m_acceptBtn = nullptr;
    QPushButton *m_rejectBtn = nullptr;
    QPushButton *m_expandBtn = nullptr;
    QVBoxLayout *m_layout;
    QWidget *m_imageContainer = nullptr;
    QLabel *m_timestampLabel = nullptr;
    QString m_rawContent;
    QStringList m_pendingHtmlBlocks;
    bool m_isCollapsed = true;
    bool m_resizePending = false;
    bool m_markdownDirty = false;
    bool m_needsResize = false;
};
