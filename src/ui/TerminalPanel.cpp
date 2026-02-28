#include "ui/TerminalPanel.h"
#include "ui/TerminalWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

TerminalPanel::TerminalPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header bar with title and buttons
    auto *header = new QWidget(this);
    header->setFixedHeight(28);
    header->setStyleSheet("background: #0e0e0e; border-top: 1px solid #141414;");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 0, 4, 0);
    headerLayout->setSpacing(4);

    auto *titleLabel = new QLabel("Terminal", header);
    titleLabel->setStyleSheet("color: #6c7086; font-size: 11px; font-weight: bold;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    auto *newBtn = new QToolButton(header);
    newBtn->setText("+");
    newBtn->setFixedSize(22, 22);
    newBtn->setStyleSheet(
        "QToolButton { color: #6c7086; background: transparent; border: none; "
        "font-size: 16px; } QToolButton:hover { color: #cdd6f4; }");
    connect(newBtn, &QToolButton::clicked, this, [this] { newTerminal(); });
    headerLayout->addWidget(newBtn);

    auto *closeBtn = new QToolButton(header);
    closeBtn->setText("\xc3\x97"); // ×
    closeBtn->setFixedSize(22, 22);
    closeBtn->setStyleSheet(
        "QToolButton { color: #6c7086; background: transparent; border: none; "
        "font-size: 14px; } QToolButton:hover { color: #f38ba8; }");
    connect(closeBtn, &QToolButton::clicked, this, &TerminalPanel::closeCurrentTerminal);
    headerLayout->addWidget(closeBtn);

    layout->addWidget(header);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabPosition(QTabWidget::South);
    m_tabWidget->setTabsClosable(false);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { border: none; background: #0e0e0e; }"
        "QTabBar { background: #0e0e0e; }"
        "QTabBar::tab { background: #0e0e0e; color: #4a4a4a; border: none; "
        "padding: 3px 12px; font-size: 11px; }"
        "QTabBar::tab:selected { color: #cdd6f4; border-top: 1px solid #cba6f7; }");
    layout->addWidget(m_tabWidget);
}

void TerminalPanel::setWorkingDirectory(const QString &dir)
{
    m_workingDir = dir;
}

TerminalWidget *TerminalPanel::newTerminal()
{
    auto *tw = new TerminalWidget(this);
    int idx = m_tabWidget->addTab(tw,
        QStringLiteral("zsh %1").arg(m_tabWidget->count() + 1));
    m_tabWidget->setCurrentIndex(idx);

    connect(tw, &TerminalWidget::titleChanged, this, [this, tw](const QString &title) {
        int i = m_tabWidget->indexOf(tw);
        if (i >= 0) {
            QString short_title = title;
            if (short_title.length() > 20)
                short_title = short_title.left(20) + "...";
            m_tabWidget->setTabText(i, short_title);
        }
    });

    connect(tw, &TerminalWidget::shellFinished, this, [this, tw](int) {
        int i = m_tabWidget->indexOf(tw);
        if (i >= 0) {
            m_tabWidget->removeTab(i);
            tw->deleteLater();
        }
        if (m_tabWidget->count() == 0)
            hide();
    });

    tw->startShell(m_workingDir);
    return tw;
}

void TerminalPanel::closeCurrentTerminal()
{
    int idx = m_tabWidget->currentIndex();
    if (idx < 0) return;

    auto *tw = qobject_cast<TerminalWidget *>(m_tabWidget->widget(idx));
    m_tabWidget->removeTab(idx);
    if (tw) {
        tw->deleteLater();
    }

    if (m_tabWidget->count() == 0)
        hide();
}

void TerminalPanel::clearCurrentTerminal()
{
    auto *tw = qobject_cast<TerminalWidget *>(m_tabWidget->currentWidget());
    if (tw) {
        tw->writeToPty(QByteArray("clear\n"));
    }
}

int TerminalPanel::terminalCount() const
{
    return m_tabWidget->count();
}
