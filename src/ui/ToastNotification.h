#pragma once
#include <QWidget>

enum class ToastType { Success, Error, Info, Warning };

// A single slide-in / fade-out toast popup.
// Created as a frameless top-level widget (Qt::Tool) so it floats
// above all panels.  ToastManager owns lifetime via deleteLater().
class ToastNotification : public QWidget {
    Q_OBJECT
public:
    explicit ToastNotification(const QString &message, ToastType type,
                               QWidget *parent = nullptr);

    // Animate in, hold for durationMs, then animate out and delete self.
    void showToast(int durationMs = 3000);

    QSize sizeHint() const override;

signals:
    void dismissed(); // emitted just before deleteLater()

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString   m_message;
    ToastType m_type;
};
