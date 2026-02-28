#include "ui/InputBar.h"
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QFocusEvent>

InputBar::InputBar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 6);
    layout->setSpacing(6);

    m_input = new QTextEdit(this);
    m_input->setObjectName("chatInput");
    m_input->setPlaceholderText("Ask Claude anything...");
    m_input->setMaximumHeight(80);
    m_input->setMinimumHeight(32);
    m_input->installEventFilter(this);

    m_sendBtn = new QPushButton("\xe2\x86\x91", this); // up arrow
    m_sendBtn->setFixedSize(32, 32);
    m_sendBtn->setStyleSheet(
        "QPushButton { background: #cba6f7; color: #0e0e0e; border: none; "
        "border-radius: 16px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background: #dbb9ff; }"
        "QPushButton:disabled { background: #252525; color: #45475a; }");

    layout->addWidget(m_input, 1);
    layout->addWidget(m_sendBtn, 0, Qt::AlignBottom);

    connect(m_sendBtn, &QPushButton::clicked, this, [this] {
        QString t = text().trimmed();
        if (!t.isEmpty()) {
            emit sendRequested(t);
            clear();
        }
    });

    // Animated focus ring
    m_focusAnim = new QVariantAnimation(this);
    m_focusAnim->setDuration(180);
    m_focusAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_focusAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        applyBorderColor(v.value<QColor>());
    });
}

void InputBar::applyBorderColor(const QColor &c)
{
    m_input->setStyleSheet(
        QStringLiteral("QTextEdit#chatInput { background: #141414; color: #cdd6f4; "
                       "border: 1px solid %1; border-radius: 8px; padding: 6px 10px; "
                       "font-size: 13px; }").arg(c.name()));
}

QString InputBar::text() const
{
    return m_input->toPlainText();
}

void InputBar::clear()
{
    m_input->clear();
}

void InputBar::setEnabled(bool enabled)
{
    m_input->setEnabled(enabled);
    m_sendBtn->setEnabled(enabled);
}

void InputBar::setPlaceholder(const QString &text)
{
    m_input->setPlaceholderText(text);
}

bool InputBar::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_input) {
        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
                m_sendBtn->click();
                return true;
            }
        } else if (event->type() == QEvent::FocusIn) {
            m_focusAnim->stop();
            m_focusAnim->setStartValue(QColor("#2a2a2a"));
            m_focusAnim->setEndValue(QColor("#cba6f7"));
            m_focusAnim->start();
        } else if (event->type() == QEvent::FocusOut) {
            m_focusAnim->stop();
            m_focusAnim->setStartValue(
                m_focusAnim->currentValue().isValid()
                    ? m_focusAnim->currentValue().value<QColor>()
                    : QColor("#cba6f7"));
            m_focusAnim->setEndValue(QColor("#2a2a2a"));
            m_focusAnim->start();
        }
    }
    return QWidget::eventFilter(obj, event);
}
