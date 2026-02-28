#include "ui/InputBar.h"
#include <QHBoxLayout>
#include <QKeyEvent>

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
        "QPushButton:disabled { background: #2a2a2a; color: #3a3a3a; }");

    layout->addWidget(m_input, 1);
    layout->addWidget(m_sendBtn, 0, Qt::AlignBottom);

    connect(m_sendBtn, &QPushButton::clicked, this, [this] {
        QString t = text().trimmed();
        if (!t.isEmpty()) {
            emit sendRequested(t);
            clear();
        }
    });
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
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            m_sendBtn->click();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
