#include "ui/SlashCommandPopup.h"
#include "ui/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>

SlashCommandPopup::SlashCommandPopup(QWidget *parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setFixedWidth(280);
    setMaximumHeight(240);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(0);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layout->addWidget(m_list);

    m_allCommands = {
        {"/clear",   "Start a new conversation", ""},
        {"/compact", "Compact conversation history", ""},
        {"/help",    "Show available commands", ""},
        {"/model",   "Switch Claude model", ""},
        {"/mode",    "Switch mode (agent/ask/plan)", ""},
        {"/diff",    "Show diff for current session", ""},
    };

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) {
        acceptSelection();
    });

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &SlashCommandPopup::applyThemeColors);
}

void SlashCommandPopup::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; color: %1; border: none; outline: none; }"
        "QListWidget::item { padding: 5px 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background: %2; color: %3; }"
        "QListWidget::item:hover:!selected { background: %4; }")
        .arg(p.text_primary.name(), p.bg_raised.name(),
             p.text_primary.name(), p.hover_raised.name()));
}

void SlashCommandPopup::paintEvent(QPaintEvent *)
{
    auto &p = ThemeManager::instance().palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
    painter.fillPath(path, p.bg_surface);
    painter.setPen(QPen(p.border_standard, 1));
    painter.drawPath(path);
}

void SlashCommandPopup::updateFilter(const QString &filter)
{
    m_list->clear();
    m_filtered.clear();

    auto &tm = ThemeManager::instance();
    QString mutedHex = tm.hex("text_muted");

    for (const auto &cmd : m_allCommands) {
        if (!filter.isEmpty() && !cmd.command.contains(filter, Qt::CaseInsensitive))
            continue;
        m_filtered.append(cmd);
        auto *item = new QListWidgetItem(m_list);
        item->setText(QStringLiteral("%1  %2").arg(cmd.command, cmd.description));
    }

    if (m_list->count() > 0)
        m_list->setCurrentRow(0);

    int h = qMin(m_list->count() * 30 + 12, 240);
    setFixedHeight(qMax(h, 40));
}

void SlashCommandPopup::selectNext()
{
    int cur = m_list->currentRow();
    if (cur < m_list->count() - 1)
        m_list->setCurrentRow(cur + 1);
}

void SlashCommandPopup::selectPrevious()
{
    int cur = m_list->currentRow();
    if (cur > 0)
        m_list->setCurrentRow(cur - 1);
}

QString SlashCommandPopup::acceptSelection()
{
    int row = m_list->currentRow();
    if (row >= 0 && row < m_filtered.size()) {
        QString cmd = m_filtered[row].command;
        emit commandSelected(cmd);
        return cmd;
    }
    return {};
}

bool SlashCommandPopup::hasSelection() const
{
    return m_list->currentRow() >= 0;
}

int SlashCommandPopup::itemCount() const
{
    return m_list->count();
}
