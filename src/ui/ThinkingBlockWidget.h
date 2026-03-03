#pragma once

#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QResizeEvent>

class ThinkingBlockWidget : public QFrame {
    Q_OBJECT
    Q_PROPERTY(float dotPhase READ dotPhase WRITE setDotPhase)
public:
    explicit ThinkingBlockWidget(QWidget *parent = nullptr);

    void appendContent(const QString &text);
    void finalize();
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }
    QString rawContent() const { return m_rawContent; }

    float dotPhase() const { return m_dotPhase; }
    void setDotPhase(float p) { m_dotPhase = p; m_dotsLabel->update(); updateDotsText(); }

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void applyThemeColors();
    void updateDotsText();
    void resizeBrowser();

    QVBoxLayout *m_layout;
    QHBoxLayout *m_headerLayout;
    QLabel *m_chevronLabel;
    QLabel *m_titleLabel;
    QLabel *m_dotsLabel;
    QTextBrowser *m_contentBrowser;
    QPropertyAnimation *m_dotAnim;

    QString m_rawContent;
    QElapsedTimer m_timer;
    float m_dotPhase = 0.0f;
    bool m_finalized = false;
    bool m_collapsed = false;
};
