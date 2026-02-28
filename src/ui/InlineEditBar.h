#pragma once

#include <QFrame>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>

class InlineEditBar : public QFrame {
    Q_OBJECT
public:
    explicit InlineEditBar(QWidget *parent = nullptr);

    void setContext(const QString &filePath, const QString &selectedCode, int lineNumber);
    void clear();
    void focusInput();

signals:
    void submitted(const QString &filePath, const QString &selectedCode, const QString &instruction);
    void cancelled();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void applyThemeColors();

    QLabel *m_contextLabel;
    QTextEdit *m_input;
    QPushButton *m_submitBtn;
    QPushButton *m_cancelBtn;
    QString m_filePath;
    QString m_selectedCode;
};
