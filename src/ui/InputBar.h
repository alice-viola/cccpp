#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QVariantAnimation>

class InputBar : public QWidget {
    Q_OBJECT
public:
    explicit InputBar(QWidget *parent = nullptr);

    QString text() const;
    void clear();
    void setEnabled(bool enabled);
    void setPlaceholder(const QString &text);

signals:
    void sendRequested(const QString &text);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void applyBorderColor(const QColor &c);

    QTextEdit *m_input;
    QPushButton *m_sendBtn;
    QVariantAnimation *m_focusAnim = nullptr;
};
