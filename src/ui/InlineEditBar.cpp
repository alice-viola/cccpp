#include "ui/InlineEditBar.h"
#include "ui/ThemeManager.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QFileInfo>

// ---------------------------------------------------------------------------
// Helper — build a page widget with a given layout
// ---------------------------------------------------------------------------
static QWidget *makePage(QLayout *layout)
{
    auto *w = new QWidget;
    w->setLayout(layout);
    return w;
}

InlineEditBar::InlineEditBar(QWidget *parent)
    : QFrame(parent)
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 6, 8, 6);
    rootLayout->setSpacing(0);

    m_stack = new QStackedWidget(this);
    rootLayout->addWidget(m_stack);

    // -----------------------------------------------------------------------
    // Page 0 — Input
    // -----------------------------------------------------------------------
    {
        auto *layout = new QVBoxLayout;
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        // Header row: context label + model selector
        auto *headerRow = new QHBoxLayout;
        headerRow->setSpacing(6);

        m_contextLabel = new QLabel(this);
        headerRow->addWidget(m_contextLabel, 1);

        m_modelCombo = new QComboBox(this);
        m_modelCombo->setObjectName("inlineEditModelCombo");
        m_modelCombo->addItem("Sonnet 4.6", QStringLiteral("claude-sonnet-4-6"));
        m_modelCombo->addItem("Opus 4.6",   QStringLiteral("claude-opus-4-6"));
        m_modelCombo->addItem("Haiku 4.5",  QStringLiteral("claude-haiku-4-5-20251001"));
        m_modelCombo->setFixedHeight(22);
        headerRow->addWidget(m_modelCombo);
        layout->addLayout(headerRow);

        // Input row: text area + buttons
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
            if (!instruction.isEmpty()) {
                QString modelId = m_modelCombo->currentData().toString();
                emit submitted(m_filePath, m_selectedCode, instruction,
                               m_startLine, m_endLine, modelId);
            }
        });
        btnLayout->addWidget(m_submitBtn);

        m_cancelBtn = new QPushButton("Esc", this);
        m_cancelBtn->setFixedSize(48, 24);
        connect(m_cancelBtn, &QPushButton::clicked, this, &InlineEditBar::cancelled);
        btnLayout->addWidget(m_cancelBtn);

        inputRow->addLayout(btnLayout);
        layout->addLayout(inputRow);

        m_inputPage = makePage(layout);
        m_stack->addWidget(m_inputPage);
    }

    // -----------------------------------------------------------------------
    // Page 1 — Processing
    // -----------------------------------------------------------------------
    {
        auto *layout = new QHBoxLayout;
        layout->setContentsMargins(0, 4, 0, 4);
        layout->setSpacing(8);

        m_processingLabel = new QLabel("Claude is editing", this);
        m_processingLabel->setObjectName("inlineProcessingLabel");
        layout->addWidget(m_processingLabel, 1);

        m_cancelProcessBtn = new QPushButton("Cancel", this);
        m_cancelProcessBtn->setObjectName("inlineCancelProcessBtn");
        m_cancelProcessBtn->setFixedSize(52, 22);
        connect(m_cancelProcessBtn, &QPushButton::clicked,
                this, &InlineEditBar::cancelRequested);
        layout->addWidget(m_cancelProcessBtn);

        m_processingPage = makePage(layout);
        m_stack->addWidget(m_processingPage);
    }

    // -----------------------------------------------------------------------
    // Page 2 — Review
    // -----------------------------------------------------------------------
    {
        auto *layout = new QHBoxLayout;
        layout->setContentsMargins(0, 4, 0, 4);
        layout->setSpacing(8);

        m_reviewLabel = new QLabel(this);
        m_reviewLabel->setObjectName("inlineReviewLabel");
        layout->addWidget(m_reviewLabel, 1);

        m_acceptBtn = new QPushButton("Accept", this);
        m_acceptBtn->setObjectName("inlineAcceptBtn");
        m_acceptBtn->setFixedSize(60, 22);
        connect(m_acceptBtn, &QPushButton::clicked, this, [this] {
            emit acceptAllRequested(m_filePath);
        });
        layout->addWidget(m_acceptBtn);

        m_rejectBtn = new QPushButton("Reject", this);
        m_rejectBtn->setObjectName("inlineRejectBtn");
        m_rejectBtn->setFixedSize(60, 22);
        connect(m_rejectBtn, &QPushButton::clicked, this, [this] {
            emit rejectAllRequested(m_filePath);
        });
        layout->addWidget(m_rejectBtn);

        m_reviewPage = makePage(layout);
        m_stack->addWidget(m_reviewPage);
    }

    // Animated dots timer for the processing page
    m_dotsTimer = new QTimer(this);
    m_dotsTimer->setInterval(400);
    connect(m_dotsTimer, &QTimer::timeout, this, &InlineEditBar::advanceDots);

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &InlineEditBar::applyThemeColors);
}

void InlineEditBar::advanceDots()
{
    m_dots = (m_dots + 1) % 4;
    m_processingLabel->setText("Claude is editing" + QString(m_dots, '.'));
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

void InlineEditBar::setContext(const QString &filePath, const QString &selectedCode,
                               int startLine, int endLine)
{
    m_filePath     = filePath;
    m_selectedCode = selectedCode;
    m_startLine    = startLine;
    m_endLine      = endLine;

    QFileInfo fi(filePath);
    int lineCount = endLine - startLine + 1;
    m_contextLabel->setText(QStringLiteral("%1:%2-%3 (%4 line%5 selected)")
        .arg(fi.fileName())
        .arg(startLine).arg(endLine)
        .arg(lineCount)
        .arg(lineCount != 1 ? "s" : ""));

    m_stack->setCurrentWidget(m_inputPage);
    m_dotsTimer->stop();
}

void InlineEditBar::setProcessing()
{
    m_dots = 0;
    m_processingLabel->setText("Claude is editing");
    m_stack->setCurrentWidget(m_processingPage);
    m_dotsTimer->start();
}

void InlineEditBar::setReviewMode()
{
    m_dotsTimer->stop();
    QFileInfo fi(m_filePath);
    int lineCount = m_endLine - m_startLine + 1;
    m_reviewLabel->setText(QStringLiteral("Review changes — %1:%2-%3 (%4 line%5)")
        .arg(fi.fileName())
        .arg(m_startLine).arg(m_endLine)
        .arg(lineCount)
        .arg(lineCount != 1 ? "s" : ""));
    m_stack->setCurrentWidget(m_reviewPage);
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

void InlineEditBar::clear()
{
    m_dotsTimer->stop();
    m_input->clear();
    m_filePath.clear();
    m_selectedCode.clear();
    m_contextLabel->clear();
    m_startLine = 0;
    m_endLine   = 0;
    m_stack->setCurrentWidget(m_inputPage);
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

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

void InlineEditBar::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();

    setStyleSheet(QStringLiteral(
        "InlineEditBar { background: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(p.bg_surface.name(), p.blue.name()));

    m_contextLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; font-family: 'JetBrains Mono'; }")
        .arg(p.text_muted.name()));

    m_modelCombo->setStyleSheet(QStringLiteral(
        "QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; "
        "padding: 1px 6px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; width: 16px; }"
        "QComboBox QAbstractItemView { background: %4; color: %2; border: 1px solid %3; "
        "selection-background-color: %5; }")
        .arg(p.bg_raised.name(), p.text_secondary.name(), p.border_standard.name(),
             p.bg_window.name(), p.blue.name()));

    m_input->setStyleSheet(QStringLiteral(
        "QTextEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 8px; padding: 4px 8px; font-size: 13px; }")
        .arg(p.bg_base.name(), p.text_primary.name(), p.border_standard.name()));

    m_submitBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
        "font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.blue.name(), p.on_accent.name(), p.lavender.name()));

    m_cancelBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; font-size: 11px; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.bg_raised.name(), p.text_muted.name(), p.hover_raised.name()));

    m_processingLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; font-family: 'JetBrains Mono'; }")
        .arg(p.blue.name()));

    m_cancelProcessBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; font-size: 11px; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.bg_raised.name(), p.text_muted.name(), p.hover_raised.name()));

    m_reviewLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; font-family: 'JetBrains Mono'; }")
        .arg(p.text_secondary.name()));

    m_acceptBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
        "font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.green.name(), p.on_accent.name(), p.green.lighter(115).name()));

    m_rejectBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
        "font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.red.name(), p.on_accent.name(), p.red.lighter(115).name()));
}
