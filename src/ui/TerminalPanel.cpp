#include "ui/TerminalPanel.h"
#include "ui/ThemeManager.h"
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
    header->setStyleSheet(QStringLiteral("background: %1; border-top: 1px solid %2;")
        .arg(ThemeManager::instance().palette().bg_base.name(),
             ThemeManager::instance().palette().bg_surface.name()));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 0, 4, 0);
    headerLayout->setSpacing(4);

    auto *titleLabel = new QLabel("Terminal", header);
    titleLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; font-weight: bold;")
        .arg(ThemeManager::instance().palette().text_muted.name()));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    auto *newBtn = new QToolButton(header);
    newBtn->setText("+");
    newBtn->setFixedSize(22, 22);
    newBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: %1; background: transparent; border: none; "
        "font-size: 16px; } QToolButton:hover { color: %2; }")
        .arg(ThemeManager::instance().palette().text_muted.name(),
             ThemeManager::instance().palette().text_primary.name()));
    connect(newBtn, &QToolButton::clicked, this, [this] { newTerminal(); });
    headerLayout->addWidget(newBtn);

    auto *closeBtn = new QToolButton(header);
    closeBtn->setText("\xc3\x97"); // ×
    closeBtn->setFixedSize(22, 22);
    closeBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: %1; background: transparent; border: none; "
        "font-size: 14px; } QToolButton:hover { color: %2; }")
        .arg(ThemeManager::instance().palette().text_muted.name(),
             ThemeManager::instance().palette().red.name()));
    connect(closeBtn, &QToolButton::clicked, this, &TerminalPanel::closeCurrentTerminal);
    headerLayout->addWidget(closeBtn);

    layout->addWidget(header);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabPosition(QTabWidget::South);
    m_tabWidget->setTabsClosable(false);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: none; background: %1; }"
        "QTabBar { background: %1; }"
        "QTabBar::tab { background: %1; color: %2; border: none; "
        "padding: 3px 12px; font-size: 11px; }"
        "QTabBar::tab:selected { color: %3; border-top: 1px solid %4; }")
        .arg(ThemeManager::instance().palette().bg_base.name(),
             ThemeManager::instance().palette().overlay0.name(),
             ThemeManager::instance().palette().text_primary.name(),
             ThemeManager::instance().palette().mauve.name()));
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
        QStringLiteral("bash %1").arg(m_tabWidget->count() + 1));
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
