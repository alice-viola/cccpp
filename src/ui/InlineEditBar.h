#pragma once

#include <QFrame>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QTimer>

class InlineEditBar : public QFrame {
    Q_OBJECT
public:
    explicit InlineEditBar(QWidget *parent = nullptr);

    // State transitions
    void setContext(const QString &filePath, const QString &selectedCode, int startLine, int endLine);
    void setProcessing();
    void setReviewMode();

    void clear();
    void focusInput();

signals:
    void submitted(const QString &filePath, const QString &selectedCode, const QString &instruction,
                   int startLine, int endLine, const QString &modelId);
    void cancelled();
    void cancelRequested();
    void acceptAllRequested(const QString &filePath);
    void rejectAllRequested(const QString &filePath);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void applyThemeColors();
    void advanceDots();

    // Pages
    QStackedWidget *m_stack;
    QWidget        *m_inputPage;
    QWidget        *m_processingPage;
    QWidget        *m_reviewPage;

    // Input page widgets
    QLabel         *m_contextLabel;
    QComboBox      *m_modelCombo;
    QTextEdit      *m_input;
    QPushButton    *m_submitBtn;
    QPushButton    *m_cancelBtn;

    // Processing page widgets
    QLabel         *m_processingLabel;
    QPushButton    *m_cancelProcessBtn;
    QTimer         *m_dotsTimer;
    int             m_dots = 0;

    // Review page widgets
    QLabel         *m_reviewLabel;
    QPushButton    *m_acceptBtn;
    QPushButton    *m_rejectBtn;

    QString m_filePath;
    QString m_selectedCode;
    int     m_startLine = 0;
    int     m_endLine   = 0;
};
