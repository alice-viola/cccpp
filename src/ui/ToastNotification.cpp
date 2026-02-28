#include "ui/ToastNotification.h"
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>

ToastNotification::ToastNotification(const QString &message, ToastType type,
                                     QWidget *parent)
    : QWidget(parent,
              Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_message(message)
    , m_type(type)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(sizeHint());
}

QSize ToastNotification::sizeHint() const
{
    QFont f = font();
    f.setPixelSize(12);
    QFontMetrics fm(f);
    int w = fm.horizontalAdvance(m_message) + 20 /*left-accent+gap*/ + 24 /*h-padding*/;
    return QSize(qBound(200, w, 380), 42);
}

void ToastNotification::showToast(int durationMs)
{
    show();
    setWindowOpacity(0.0);

    // Fade in
    auto *fadeIn = new QPropertyAnimation(this, "windowOpacity", this);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setDuration(200);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    // Hold, then fade out
    QTimer::singleShot(durationMs, this, [this] {
        auto *fadeOut = new QPropertyAnimation(this, "windowOpacity", this);
        fadeOut->setStartValue(1.0);
        fadeOut->setEndValue(0.0);
        fadeOut->setDuration(300);
        fadeOut->setEasingCurve(QEasingCurve::InCubic);
        connect(fadeOut, &QPropertyAnimation::finished, this, [this] {
            emit dismissed();
            deleteLater();
        });
        fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

void ToastNotification::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);

    // Drop shadow (cheap: slightly offset filled rect)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 50));
    p.drawRoundedRect(r.adjusted(2, 2, 2, 2), 8, 8);

    // Card background + border
    p.setBrush(QColor("#1e1e1e"));
    p.setPen(QPen(QColor("#333333"), 1));
    p.drawRoundedRect(r, 8, 8);

    // Left accent bar
    QColor accent;
    switch (m_type) {
    case ToastType::Success: accent = QColor("#a6e3a1"); break;
    case ToastType::Error:   accent = QColor("#f38ba8"); break;
    case ToastType::Warning: accent = QColor("#f9e2af"); break;
    default:                 accent = QColor("#89b4fa"); break;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    QRect bar(r.left() + 1, r.top() + 6, 3, r.height() - 12);
    p.drawRoundedRect(bar, 2, 2);

    // Message text
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(QColor("#cdd6f4"));
    QRect textRect = r.adjusted(16, 0, -8, 0);
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, m_message);
}
