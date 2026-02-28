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

    m_headerWidget = new QWidget(this);
    m_headerWidget->setFixedHeight(28);
    auto *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(8, 0, 4, 0);
    headerLayout->setSpacing(4);

    m_titleLabel = new QLabel("Terminal", m_headerWidget);
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    m_newBtn = new QToolButton(m_headerWidget);
    m_newBtn->setText("+");
    m_newBtn->setFixedSize(22, 22);
    connect(m_newBtn, &QToolButton::clicked, this, [this] { newTerminal(); });
    headerLayout->addWidget(m_newBtn);

    m_closeBtn = new QToolButton(m_headerWidget);
    m_closeBtn->setText("\xc3\x97");
    m_closeBtn->setFixedSize(22, 22);
    connect(m_closeBtn, &QToolButton::clicked, this, &TerminalPanel::closeCurrentTerminal);
    headerLayout->addWidget(m_closeBtn);

    layout->addWidget(m_headerWidget);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabPosition(QTabWidget::South);
    m_tabWidget->setTabsClosable(false);
    m_tabWidget->setDocumentMode(true);
    layout->addWidget(m_tabWidget);

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &TerminalPanel::applyThemeColors);
}

void TerminalPanel::applyThemeColors()
{
    const auto &pal = ThemeManager::instance().palette();

    m_headerWidget->setStyleSheet(QStringLiteral("background: %1; border-top: 1px solid %2;")
        .arg(pal.bg_base.name(), pal.bg_surface.name()));

    m_titleLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; font-weight: bold;")
        .arg(pal.text_muted.name()));

    m_newBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: %1; background: transparent; border: none; "
        "font-size: 16px; } QToolButton:hover { color: %2; }")
        .arg(pal.text_muted.name(), pal.text_primary.name()));

    m_closeBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: %1; background: transparent; border: none; "
        "font-size: 14px; } QToolButton:hover { color: %2; }")
        .arg(pal.text_muted.name(), pal.red.name()));

    m_tabWidget->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: none; background: %1; }"
        "QTabBar { background: %1; }"
        "QTabBar::tab { background: %1; color: %2; border: none; "
        "padding: 3px 12px; font-size: 11px; }"
        "QTabBar::tab:selected { color: %3; border-top: 1px solid %4; }")
        .arg(pal.bg_base.name(), pal.overlay0.name(),
             pal.text_primary.name(), pal.mauve.name()));
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
