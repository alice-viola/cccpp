#include "ui/ThinkingIndicator.h"
#include "ui/ThemeManager.h"
#include <QPainter>
#include <cmath>

static constexpr float TWO_PI = 6.28318530718f;

ThinkingIndicator::ThinkingIndicator(QWidget *parent)
    : QWidget(parent)
{
    m_anim = new QPropertyAnimation(this, "phase", this);
    m_anim->setStartValue(0.0f);
    m_anim->setEndValue(TWO_PI);
    m_anim->setDuration(1400);
    m_anim->setEasingCurve(QEasingCurve::Linear);
    m_anim->setLoopCount(-1);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this] { update(); });

    hide();
}

void ThinkingIndicator::startAnimation()
{
    show();
    m_anim->start();
}

void ThinkingIndicator::stopAnimation()
{
    m_anim->stop();
    hide();
}

QSize ThinkingIndicator::sizeHint() const
{
    return QSize(60, 30);
}

void ThinkingIndicator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    constexpr int dotSize = 7;
    constexpr int dotGap  = 7;
    constexpr int totalW  = 3 * dotSize + 2 * dotGap;

    int startX = (width() - totalW) / 2;
    int cy     = height() / 2;

    for (int i = 0; i < 3; ++i) {
        // Each dot is offset by 1/3 of the cycle, creating a rolling wave
        float offset = m_phase - i * (TWO_PI / 3.0f);
        float wave   = 0.5f + 0.5f * sinf(offset);        // 0..1
        float alpha  = 0.20f + 0.80f * wave;
        int   bounce = static_cast<int>(5.0f * wave);      // 0..5 px upward

        QColor mc = ThemeManager::instance().palette().mauve;
        QColor c(mc.red(), mc.green(), mc.blue(), static_cast<int>(alpha * 255.0f));
        p.setBrush(c);
        p.setPen(Qt::NoPen);

        int x = startX + i * (dotSize + dotGap);
        p.drawEllipse(x, cy - dotSize / 2 - bounce, dotSize, dotSize);
    }
}
