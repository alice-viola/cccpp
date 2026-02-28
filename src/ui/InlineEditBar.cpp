#include "ui/InlineEditBar.h"
#include "ui/ThemeManager.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QFileInfo>

InlineEditBar::InlineEditBar(QWidget *parent)
    : QFrame(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    m_contextLabel = new QLabel(this);
    layout->addWidget(m_contextLabel);

    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(4);

    m_input = new QTextEdit(this);
    m_input->setObjectName("inlineEditInput");
    m_input->setPlaceholderText("Describe the change... (Enter to submit, Esc to cancel)");
    m_input->setMaximumHeight(60);
    m_input->setMinimumHeight(28);
    m_input->setAcceptRichText(false);
    m_input->installEventFilter(this);
    inputRow->addWidget(m_input, 1);

    auto *btnLayout = new QVBoxLayout;
    btnLayout->setSpacing(2);

    m_submitBtn = new QPushButton("Edit", this);
    m_submitBtn->setFixedSize(48, 24);
    connect(m_submitBtn, &QPushButton::clicked, this, [this] {
        QString instruction = m_input->toPlainText().trimmed();
        if (!instruction.isEmpty())
            emit submitted(m_filePath, m_selectedCode, instruction);
    });
    btnLayout->addWidget(m_submitBtn);

    m_cancelBtn = new QPushButton("Esc", this);
    m_cancelBtn->setFixedSize(48, 24);
    connect(m_cancelBtn, &QPushButton::clicked, this, &InlineEditBar::cancelled);
    btnLayout->addWidget(m_cancelBtn);

    inputRow->addLayout(btnLayout);
    layout->addLayout(inputRow);

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &InlineEditBar::applyThemeColors);
}

void InlineEditBar::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();

    setStyleSheet(QStringLiteral(
        "InlineEditBar { background: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(p.bg_surface.name(), p.blue.name()));

    m_contextLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; font-family: monospace; }")
        .arg(p.text_muted.name()));

    m_input->setStyleSheet(QStringLiteral(
        "QTextEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 6px; padding: 4px 8px; font-size: 13px; }")
        .arg(p.bg_base.name(), p.text_primary.name(), p.border_standard.name()));

    m_submitBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
        "font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.blue.name(), p.on_accent.name(), p.lavender.name()));

    m_cancelBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
        "font-size: 11px; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.bg_raised.name(), p.text_muted.name(), p.hover_raised.name()));
}

void InlineEditBar::setContext(const QString &filePath, const QString &selectedCode, int lineNumber)
{
    m_filePath = filePath;
    m_selectedCode = selectedCode;

    QFileInfo fi(filePath);
    int lineCount = selectedCode.split('\n').size();
    m_contextLabel->setText(QStringLiteral("%1:%2 (%3 line%4 selected)")
        .arg(fi.fileName())
        .arg(lineNumber)
        .arg(lineCount)
        .arg(lineCount != 1 ? "s" : ""));
}

void InlineEditBar::clear()
{
    m_input->clear();
    m_filePath.clear();
    m_selectedCode.clear();
    m_contextLabel->clear();
}

void InlineEditBar::focusInput()
{
    m_input->setFocus();
}

bool InlineEditBar::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            m_submitBtn->click();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            emit cancelled();
            return true;
        }
    }
    return QFrame::eventFilter(obj, event);
}
