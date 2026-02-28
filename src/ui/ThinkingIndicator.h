#pragma once
#include <QWidget>
#include <QPropertyAnimation>

// Animated 3-dot bouncing indicator shown while Claude processes a response.
// Uses a Q_PROPERTY-driven sine wave so QPropertyAnimation can drive paintEvent.
class ThinkingIndicator : public QWidget {
    Q_OBJECT
    Q_PROPERTY(float phase READ phase WRITE setPhase)
public:
    explicit ThinkingIndicator(QWidget *parent = nullptr);

    void startAnimation();
    void stopAnimation();

    float phase() const { return m_phase; }
    void setPhase(float p) { m_phase = p; update(); }

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override;

private:
    QPropertyAnimation *m_anim;
    float m_phase = 0.0f;
};
