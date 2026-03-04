#include "ui/FindBar.h"

FindBar::FindBar(QWidget *parent) : QWidget(parent) {
    const auto &pal = ThemeManager::instance().palette();
    setStyleSheet(
        QStringLiteral(
            "FindBar { background: %1; border-top: 1px solid %2; }"
            "QLineEdit { background: %3; color: %4; border: 1px solid %2;"
            "  border-radius: 4px; padding: 3px 6px; font-size: 12px;"
            "  font-family: 'JetBrains Mono'; }"
            "QLineEdit:focus { border-color: %5; }"
            "QPushButton { background: transparent; border: none;"
            "  color: %6; font-size: 13px; padding: 2px 6px; }"
            "QPushButton:hover { color: %4; }"
            "QLabel { color: %6; font-size: 11px; }")
            .arg(pal.bg_surface.name(), pal.border_standard.name(),
                 pal.bg_raised.name(), pal.text_primary.name(),
                 pal.teal.name(), pal.text_muted.name()));

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(4);

    auto *closeBtn = new QPushButton(QString::fromUtf8("\xc3\x97"), this);
    closeBtn->setFixedSize(22, 22);
    closeBtn->setToolTip("Close (Escape)");
    connect(closeBtn, &QPushButton::clicked, this, [this] {
        if (m_closeCb) m_closeCb();
    });

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText("Find...");
    m_input->setMinimumWidth(160);
    m_input->installEventFilter(this);

    auto *prevBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xb2"), this);
    prevBtn->setFixedSize(22, 22);
    prevBtn->setToolTip("Previous match (Shift+Return)");

    auto *nextBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xbc"), this);
    nextBtn->setFixedSize(22, 22);
    nextBtn->setToolTip("Next match (Return)");

    m_countLabel = new QLabel(this);

    lay->addWidget(closeBtn);
    lay->addWidget(m_input, 1);
    lay->addWidget(prevBtn);
    lay->addWidget(nextBtn);
    lay->addWidget(m_countLabel);

    connect(m_input, &QLineEdit::textChanged, this, &FindBar::doFind);
    connect(m_input, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(nextBtn, &QPushButton::clicked, this, [this] { findNext(); });
    connect(prevBtn, &QPushButton::clicked, this, [this] { findPrev(); });

    setFixedHeight(34);
}

void FindBar::focusInput() {
    m_input->setFocus();
    m_input->selectAll();
}

void FindBar::prefill(const QString &text) {
    if (!text.isEmpty())
        m_input->setText(text);
}

#ifndef NO_QSCINTILLA
void FindBar::setEditor(QsciScintilla *ed) {
    m_editor = ed;
    doFind(m_input->text());
}
#endif

bool FindBar::eventFilter(QObject *obj, QEvent *event) {
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            if (m_closeCb) m_closeCb();
            return true;
        }
        if ((ke->modifiers() & Qt::ShiftModifier) && ke->key() == Qt::Key_Return) {
            findPrev();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void FindBar::doFind(const QString &text) {
    if (text.isEmpty()) {
        m_countLabel->clear();
        return;
    }
#ifndef NO_QSCINTILLA
    if (m_editor) {
        bool found = m_editor->findFirst(text, false, false, false, true, true, 0, 0);
        int count = 0;
        if (found) {
            QString editorText = m_editor->text();
            int pos = 0;
            while ((pos = editorText.indexOf(text, pos, Qt::CaseInsensitive)) != -1) {
                ++count;
                pos += text.length();
            }
        }
        m_countLabel->setText(count > 0 ? QString("%1 found").arg(count) : "No results");
    }
#endif
}

void FindBar::findNext() {
#ifndef NO_QSCINTILLA
    if (m_editor && !m_input->text().isEmpty())
        m_editor->findFirst(m_input->text(), false, false, false, true, true);
#endif
}

void FindBar::findPrev() {
#ifndef NO_QSCINTILLA
    if (m_editor && !m_input->text().isEmpty())
        m_editor->findFirst(m_input->text(), false, false, false, true, false);
#endif
}
