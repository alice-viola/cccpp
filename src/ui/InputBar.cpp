#include "ui/InputBar.h"
#include "ui/ThemeManager.h"
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

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &InputBar::applyThemeColors);
}

void InputBar::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();
    m_sendBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; "
        "border-radius: 14px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:disabled { background: %4; color: %5; }")
        .arg(p.blue.name(), p.on_accent.name(), p.lavender.name(),
             p.bg_raised.name(), p.text_faint.name()));
}

void InputBar::applyBorderColor(const QColor &c)
{
    auto &p = ThemeManager::instance().palette();
    m_input->setStyleSheet(
        QStringLiteral("QTextEdit#chatInput { background: %1; color: %2; "
                       "border: 1px solid %3; border-radius: 12px; padding: 6px 10px; "
                       "font-size: 13px; }").arg(p.bg_surface.name(), p.text_primary.name(), c.name()));
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
            auto &p = ThemeManager::instance().palette();
            m_focusAnim->stop();
            m_focusAnim->setStartValue(p.border_standard);
            m_focusAnim->setEndValue(p.mauve);
            m_focusAnim->start();
        } else if (event->type() == QEvent::FocusOut) {
            auto &p = ThemeManager::instance().palette();
            m_focusAnim->stop();
            m_focusAnim->setStartValue(
                m_focusAnim->currentValue().isValid()
                    ? m_focusAnim->currentValue().value<QColor>()
                    : p.mauve);
            m_focusAnim->setEndValue(p.border_standard);
            m_focusAnim->start();
        }
    }
    return QWidget::eventFilter(obj, event);
}
