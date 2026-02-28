#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QVariantAnimation>
#include <QLabel>
#include <QList>
#include <QPair>

class ContextPopup;
class SlashCommandPopup;

struct AttachedContext {
    QString displayName;
    QString fullPath;
};

struct AttachedImage {
    QByteArray data;
    QString format;
    QString displayName;
};

class InputBar : public QWidget {
    Q_OBJECT
public:
    explicit InputBar(QWidget *parent = nullptr);

    QString text() const;
    void clear();
    void setEnabled(bool enabled);
    void setPlaceholder(const QString &text);

    void setWorkspacePath(const QString &path);
    void setOpenFiles(const QStringList &files);
    void setRecentFiles(const QStringList &files);

    QList<AttachedContext> attachedContexts() const { return m_attachedContexts; }
    QList<AttachedImage> attachedImages() const { return m_attachedImages; }
    void clearAttachments();

    void setContextIndicator(const QString &text);

signals:
    void sendRequested(const QString &text);
    void slashCommand(const QString &command, const QString &args);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void applyThemeColors();
    void applyBorderColor(const QColor &c);
    void showContextPopup();
    void hideContextPopup();
    void showSlashPopup();
    void hideSlashPopup();
    void onContextItemSelected(const QString &displayName, const QString &fullPath);
    void updateContextPills();
    void addImageThumbnail(const QByteArray &data, const QString &format, const QString &name);

    QTextEdit *m_input;
    QPushButton *m_sendBtn;
    QVariantAnimation *m_focusAnim = nullptr;
    ContextPopup *m_contextPopup = nullptr;
    SlashCommandPopup *m_slashPopup = nullptr;
    QWidget *m_contextPillBar = nullptr;
    QLabel *m_contextIndicator = nullptr;
    QWidget *m_imageBar = nullptr;

    QString m_workspacePath;
    QStringList m_openFiles;
    QStringList m_recentFiles;
    QList<AttachedContext> m_attachedContexts;
    QList<AttachedImage> m_attachedImages;

    int m_atTriggerPos = -1;
    bool m_popupActive = false;
    bool m_slashPopupActive = false;
};
