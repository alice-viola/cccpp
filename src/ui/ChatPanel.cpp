#include "ui/ChatPanel.h"
#include "ui/InputBar.h"
#include "ui/ModeSelector.h"
#include "ui/ModelSelector.h"
#include "ui/ProfileSelector.h"
#include "ui/ProfileEditorDialog.h"
#include "core/PersonalityProfile.h"
#include "ui/ChatMessageWidget.h"
#include "ui/ToolCallGroupWidget.h"
#include "ui/ThinkingIndicator.h"
#include "ui/ThinkingBlockWidget.h"
#include "ui/QuestionWidget.h"
#include "ui/SuggestionChips.h"
#include "ui/ThemeManager.h"
#include "ui/CodeViewer.h"
#include "core/ClaudeProcess.h"
#include "core/StreamParser.h"
#include "core/SessionManager.h"
#include "core/DiffEngine.h"
#include "core/Database.h"
#include "util/JsonUtils.h"
#include <algorithm>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QMenu>
#include <QMessageBox>
#include <QFileDialog>
#include <QWidgetAction>
#include <QTabBar>
#include <QPainter>
#include <QDebug>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

// ---------------------------------------------------------------------------
// Welcome state widget — shown in empty chat tabs
// ---------------------------------------------------------------------------

class ChatWelcomeState : public QWidget {
public:
    explicit ChatWelcomeState(QWidget *parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAttribute(Qt::WA_TranslucentBackground);

        auto *layout = new QVBoxLayout(this);
        layout->setAlignment(Qt::AlignCenter);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Spacer absorbs space above the painted area
        layout->addStretch(1);

        // Fixed-height region where icon is painted
        m_paintArea = new QWidget(this);
        m_paintArea->setFixedHeight(80);
        m_paintArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_paintArea->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(m_paintArea);

        // Title
        auto &pal = ThemeManager::instance().palette();
        m_title = new QLabel("What can I help you with?", this);
        m_title->setAlignment(Qt::AlignCenter);
        m_title->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 16px; font-weight: 500; background: transparent; }")
            .arg(pal.text_primary.name()));
        layout->addWidget(m_title);

        layout->addSpacing(6);

        // Subtitle
        m_subtitle = new QLabel("Ask me to build features, debug issues, refactor code,\nor explore this codebase.", this);
        m_subtitle->setAlignment(Qt::AlignCenter);
        m_subtitle->setWordWrap(true);
        m_subtitle->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 12px; background: transparent; }")
            .arg(pal.text_muted.name()));
        layout->addWidget(m_subtitle);

        layout->addSpacing(20);

        // Chips label
        auto *chipsLabel = new QLabel("Suggestions", this);
        chipsLabel->setAlignment(Qt::AlignCenter);
        chipsLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 10px; font-weight: 600; "
            "letter-spacing: 1px; text-transform: uppercase; background: transparent; }")
            .arg(pal.text_faint.name()));
        layout->addWidget(chipsLabel);

        layout->addSpacing(8);

        m_chips = new SuggestionChips(this);
        m_chips->setSuggestions({
            "Survey this codebase",
            "Find bugs in recent changes",
            "Write tests for...",
            "Refactor..."
        });
        layout->addWidget(m_chips, 0, Qt::AlignCenter);

        // Spacer below chips
        layout->addStretch(2);
    }

    SuggestionChips *chips() const { return m_chips; }

protected:
    void paintEvent(QPaintEvent *) override {
        const auto &pal = ThemeManager::instance().palette();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Icon center — middle of the paint area widget
        QRect pa = m_paintArea->geometry();
        int cx = pa.center().x();
        int iconCy = pa.center().y();

        // Outer ring arc (decorative, partial — 220°)
        p.setPen(QPen(pal.surface1, 1.0));
        p.setBrush(Qt::NoBrush);
        QRect outerRect(cx - 28, iconCy - 28, 56, 56);
        p.drawArc(outerRect, 160 * 16, 220 * 16);

        // Middle ring arc (teal accent — 180°)
        p.setPen(QPen(pal.teal, 1.5));
        QRect midRect(cx - 20, iconCy - 20, 40, 40);
        p.drawArc(midRect, 180 * 16, 180 * 16);

        // Inner circle (agent node)
        p.setPen(QPen(pal.teal, 1.5));
        p.setBrush(pal.bg_raised);
        p.drawEllipse(QPointF(cx, iconCy), 8, 8);

        // Center dot
        p.setPen(Qt::NoPen);
        p.setBrush(pal.teal);
        p.drawEllipse(QPointF(cx, iconCy), 3, 3);

        // Satellite dots at 45° and 225°
        p.setBrush(pal.surface2);
        double r = 34.0;
        double d = r * 0.707;
        p.drawEllipse(QPointF(cx + d, iconCy - d), 2, 2);
        p.drawEllipse(QPointF(cx - d, iconCy + d), 2, 2);
    }

private:
    QWidget *m_paintArea = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    SuggestionChips *m_chips = nullptr;
};

// ---------------------------------------------------------------------------

ChatPanel::ChatPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_scrollDebounce = new QTimer(this);
    m_scrollDebounce->setSingleShot(true);
    m_scrollDebounce->setInterval(100);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setDocumentMode(true);

    auto *cornerWidget = new QWidget(this);
    auto *cornerLayout = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(0);

    m_newChatBtn = new QPushButton("\xe2\x9e\x95", this);  // +
    m_newChatBtn->setFixedHeight(26);
    m_newChatBtn->setToolTip("New Chat (Ctrl+N)");
    connect(m_newChatBtn, &QPushButton::clicked, this, [this] { newChat(); });

    m_historyBtn = new QPushButton("History", this);
    m_historyBtn->setFixedHeight(26);
    m_historyBtn->setToolTip("Browse previous chats");
    connect(m_historyBtn, &QPushButton::clicked, this, &ChatPanel::showHistoryMenu);

    m_plansBtn = new QPushButton("Plans", this);
    m_plansBtn->setFixedHeight(26);
    m_plansBtn->setToolTip("Browse Claude plans");
    connect(m_plansBtn, &QPushButton::clicked, this, &ChatPanel::showPlansMenu);

    cornerLayout->addWidget(m_newChatBtn);
    cornerLayout->addWidget(m_historyBtn);
    cornerLayout->addWidget(m_plansBtn);
    m_tabWidget->setCornerWidget(cornerWidget, Qt::TopRightCorner);

    mainLayout->addWidget(m_tabWidget, 1);

    m_modeSelector = new ModeSelector(this);
    m_modelSelector = new ModelSelector(this);
    m_profileSelector = new ProfileSelector(this);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statsLabel->setFixedHeight(18);
    m_statsLabel->setContentsMargins(0, 0, 8, 0);
    m_statsLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 10px; }");
    m_statsLabel->hide();
    mainLayout->addWidget(m_statsLabel);

    // Orchestrator toggle
    m_orchestratorToggle = new QPushButton(this);
    m_orchestratorToggle->setCheckable(true);
    m_orchestratorToggle->setChecked(false);
    m_orchestratorToggle->setCursor(Qt::PointingHandCursor);
    m_orchestratorToggle->setFixedHeight(24);
    m_orchestratorToggle->setToolTip("Orchestrate: delegate work to specialist agents");
    auto updateOrcToggleStyle = [this] {
        const auto &pal = ThemeManager::instance().palette();
        bool on = m_orchestratorToggle->isChecked();
        m_orchestratorToggle->setText(on ? "\xe2\x9a\x99 Orchestrate \u25BE" : "\xe2\x9a\x99 Orchestrate");
        QColor fg = on ? pal.mauve : pal.text_muted;
        QColor bg = on ? QColor(pal.mauve.red(), pal.mauve.green(), pal.mauve.blue(), 25)
                       : Qt::transparent;
        m_orchestratorToggle->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 4px; padding: 4px 10px; font-size: 12px; }"
            "QPushButton:hover { color: %4; }")
            .arg(bg.name(QColor::HexArgb), fg.name(),
                 on ? pal.mauve.name() : QString("transparent"),
                 pal.text_primary.name()));
    };
    updateOrcToggleStyle();
    connect(m_orchestratorToggle, &QPushButton::toggled, this, [updateOrcToggleStyle] {
        updateOrcToggleStyle();
    });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [updateOrcToggleStyle] { updateOrcToggleStyle(); });

    m_inputBar = new InputBar(this);
    m_inputBar->addFooterWidget(m_modeSelector);
    m_inputBar->addFooterWidget(m_modelSelector);
    m_inputBar->addFooterWidget(m_profileSelector);
    m_inputBar->addFooterWidget(m_orchestratorToggle);
    mainLayout->addWidget(m_inputBar);

    connect(m_inputBar, &InputBar::sendRequested, this, &ChatPanel::onSendRequested);
    connect(m_inputBar, &InputBar::stopRequested, this, [this] {
        int idx = m_tabWidget->currentIndex();
        if (m_tabs.contains(idx) && m_tabs[idx].processing) {
            m_tabs[idx].process->cancel();
        }
    });
    connect(m_inputBar, &InputBar::slashCommand, this, &ChatPanel::onSlashCommand);

    // Keep current tab's profileIds in sync with ProfileSelector
    connect(m_profileSelector, &ProfileSelector::selectionChanged, this, [this](const QStringList &ids) {
        int idx = m_tabWidget->currentIndex();
        if (m_tabs.contains(idx))
            m_tabs[idx].profileIds = ids;
    });

    // Open profile editor dialog
    connect(m_profileSelector, &ProfileSelector::manageProfilesRequested, this, [this] {
        ProfileEditorDialog dlg(m_workingDir, this);
        dlg.exec();
    });
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int idx) {
        refreshInputBarForCurrentTab();
        updateStatsLabel();
        if (m_tabs.contains(idx)) {
            m_tabs[idx].unread = false;
            emit activeSessionChanged(m_tabs[idx].sessionId);
        }
        // Update only the previous and current tab icons (not all)
        if (m_previousTabIndex >= 0 && m_tabs.contains(m_previousTabIndex))
            updateTabIcon(m_previousTabIndex);
        if (m_tabs.contains(idx))
            updateTabIcon(idx);
        m_previousTabIndex = idx;
    });
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, [this](int idx) {
        if (m_tabs.size() <= 1) return;
        m_tabs.remove(idx);
        m_tabWidget->removeTab(idx);
        QMap<int, ChatTab> reindexed;
        int i = 0;
        for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it, ++i) {
            it.value().tabIndex = i;
            reindexed[i] = it.value();
        }
        m_tabs = reindexed;
        emit sessionListChanged();
    });

    m_tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabWidget->tabBar(), &QWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        int tabIdx = m_tabWidget->tabBar()->tabAt(pos);
        if (tabIdx < 0 || !m_tabs.contains(tabIdx)) return;
        QString sid = m_tabs[tabIdx].sessionId;

        QMenu menu(this);
        auto *exportAction = menu.addAction("Export as JSON");
        connect(exportAction, &QAction::triggered, this, [this, sid] {
            exportChatHistory(sid);
        });
        auto *delAction = menu.addAction("Delete Chat");
        connect(delAction, &QAction::triggered, this, [this, sid] {
            deleteSession(sid);
        });
        menu.exec(m_tabWidget->tabBar()->mapToGlobal(pos));
    });

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ChatPanel::applyThemeColors);
}

void ChatPanel::applyThemeColors()
{
    auto &thm = ThemeManager::instance();
    auto cornerBtnStyle = QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; "
        "font-size: 11px; padding: 0 8px; }"
        "QPushButton:hover { color: %2; }")
        .arg(thm.hex("text_muted"), thm.hex("text_secondary"));
    m_newChatBtn->setStyleSheet(cornerBtnStyle);
    m_historyBtn->setStyleSheet(cornerBtnStyle);
    m_plansBtn->setStyleSheet(cornerBtnStyle);

    m_tabWidget->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: none; background: %1; }"
        "QTabBar { background: %1; border-bottom: 1px solid %2; }"
        "QTabBar::tab { background: transparent; color: %3; border: none; "
        "  padding: 4px 12px; font-size: 12px; border-radius: 8px; margin: 2px 1px; }"
        "QTabBar::tab:selected { color: %4; background: %5; }"
        "QTabBar::tab:hover:!selected { color: %6; background: %7; }"
        "QTabBar::close-button { subcontrol-position: right; padding: 2px; }"
        "QTabBar::close-button:hover { background: %8; border-radius: 4px; }")
        .arg(thm.hex("bg_window"), thm.hex("border_subtle"), thm.hex("text_muted"),
             thm.hex("text_primary"), thm.hex("bg_raised"), thm.hex("text_secondary"),
             thm.hex("bg_surface"), thm.hex("red_30pct")));

    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it)
        updateTabIcon(it.key());
}

void ChatPanel::setSessionManager(SessionManager *mgr) { m_sessionMgr = mgr; }
void ChatPanel::setDiffEngine(DiffEngine *diff) { m_diffEngine = diff; }
void ChatPanel::setDatabase(Database *db) { m_database = db; }
void ChatPanel::setWorkingDirectory(const QString &dir) {
    m_workingDir = dir;
    // Strip trailing slash to avoid off-by-one in relative path computation
    while (m_workingDir.endsWith('/') && m_workingDir.length() > 1)
        m_workingDir.chop(1);
    m_inputBar->setWorkspacePath(m_workingDir);
}
void ChatPanel::setCodeViewer(CodeViewer *viewer) { m_codeViewer = viewer; }

void ChatPanel::wireProcessSignals(ChatTab &tab)
{
    ClaudeProcess *proc = tab.process;

    connect(proc->streamParser(), &StreamParser::textDelta, this,
            [this, proc](const QString &text) {
        auto *t = tabForProcess(proc);
        if (!t) return;

        if (t->currentToolGroup) {
            t->currentToolGroup->finalize();
            t->currentToolGroup = nullptr;
        }

        if (!t->currentAssistantMsg) {
            t->currentAssistantMsg = new ChatMessageWidget(ChatMessageWidget::Assistant, "");
            t->currentAssistantMsg->setTurnId(t->turnId);
            t->currentAssistantMsg->setTimestamp(QDateTime::currentDateTime());
            if (!t->hasFirstAssistantMsg)
                t->hasFirstAssistantMsg = true;
            else
                t->currentAssistantMsg->setHeaderVisible(false);
            addMessageToTab(*t, t->currentAssistantMsg);
        }
        t->pendingText += text;
        t->accumulatedRawContent += text;
        // appendContent is batched by textFlushTimer; no direct call here
    });

    connect(proc->streamParser(), &StreamParser::thinkingStarted, this,
            [this, proc] {
        auto *t = tabForProcess(proc);
        if (!t) return;
        flushPendingText(*t);
        saveCurrentTextSegment(*t);
        if (t->currentAssistantMsg)
            t->currentAssistantMsg->finalizeContent();
        t->currentAssistantMsg = nullptr;
        if (t->currentToolGroup) {
            t->currentToolGroup->finalize();
            t->currentToolGroup = nullptr;
        }
        t->currentThinkingBlock = new ThinkingBlockWidget;
        if (t->messagesLayout)
            t->messagesLayout->insertWidget(insertPosForTab(*t), t->currentThinkingBlock);
        scrollTabToBottom(*t);
    });

    connect(proc->streamParser(), &StreamParser::thinkingDelta, this,
            [this, proc](const QString &text) {
        auto *t = tabForProcess(proc);
        if (!t) return;
        if (t->currentThinkingBlock)
            t->currentThinkingBlock->appendContent(text);
        scrollTabToBottom(*t);
    });

    connect(proc->streamParser(), &StreamParser::thinkingStopped, this,
            [this, proc] {
        auto *t = tabForProcess(proc);
        if (!t) return;
        if (t->currentThinkingBlock) {
            t->currentThinkingBlock->finalize();
            if (m_database) {
                MessageRecord rec;
                rec.sessionId = t->sessionId;
                rec.role = "thinking";
                rec.content = t->currentThinkingBlock->rawContent();
                rec.turnId = t->turnId;
                rec.timestamp = QDateTime::currentSecsSinceEpoch();
                m_database->saveMessage(rec);
            }
            t->currentThinkingBlock = nullptr;
        }
    });

    connect(proc->streamParser(), &StreamParser::editStreamStarted, this,
            [this, proc](const QString &toolName, const QString &filePath) {
        Q_UNUSED(toolName);
        auto *t = tabForProcess(proc);
        if (!t) return;
        if (!filePath.isEmpty()) {
            QString fullPath = filePath;
            if (!QFileInfo(fullPath).isAbsolute())
                fullPath = m_workingDir + "/" + filePath;
            t->pendingEditFile = fullPath;
        }
    });

    connect(proc->streamParser(), &StreamParser::checkpointReceived, this,
            [this, proc](const QString &uuid) {
        auto *t = tabForProcess(proc);
        if (!t) return;
        if (!uuid.isEmpty() && m_database) {
            CheckpointRecord cp;
            cp.sessionId = t->sessionId;
            cp.turnId = t->turnId;
            cp.uuid = uuid;
            cp.timestamp = QDateTime::currentSecsSinceEpoch();
            m_database->saveCheckpoint(cp);
        }
    });

    connect(proc->streamParser(), &StreamParser::toolUseStarted, this,
            [this, proc](const QString &name, const QString &toolId, const nlohmann::json &input) {
        auto *t = tabForProcess(proc);
        if (!t) return;

        flushPendingText(*t);
        saveCurrentTextSegment(*t);
        if (t->currentAssistantMsg)
            t->currentAssistantMsg->finalizeContent();
        t->currentAssistantMsg = nullptr;

        ToolCallInfo info;
        info.toolName = name;

        if (input.contains("path"))
            info.filePath = JsonUtils::getString(input, "path");
        else if (input.contains("file_path"))
            info.filePath = JsonUtils::getString(input, "file_path");

        info.summary = name;
        if (!info.filePath.isEmpty())
            info.summary += ": " + info.filePath;
        else if (input.contains("command"))
            info.summary += ": " + JsonUtils::getString(input, "command");

        // Track activity for Agent Fleet
        t->lastActivity = info.summary;
        t->updatedAt = QDateTime::currentSecsSinceEpoch();
        emit agentActivityChanged(t->sessionId, info.summary);

        bool isEditTool = false;

        if ((name == "Edit" || name == "StrReplace") && input.contains("old_string")) {
            isEditTool = true;
            info.isEdit = true;
            info.oldString = JsonUtils::getString(input, "old_string");
            info.newString = JsonUtils::getString(input, "new_string");

            if (m_diffEngine)
                m_diffEngine->recordEditToolChange(info.filePath, info.oldString, info.newString, t->sessionId);
            t->pendingEditFile = info.filePath;
            t->editCount++;
            emit fileChanged(info.filePath);

            int editLine = 0;
            if (!info.oldString.isEmpty()) {
                QString fullPath = info.filePath;
                if (!QFileInfo(fullPath).isAbsolute() && !m_workingDir.isEmpty())
                    fullPath = m_workingDir + "/" + fullPath;
                QFile f(fullPath);
                if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QString content = QString::fromUtf8(f.readAll());
                    int pos = content.indexOf(info.oldString);
                    if (pos >= 0)
                        editLine = content.left(pos).count('\n');
                }
            }
            emit editApplied(info.filePath, info.oldString, info.newString, editLine);
        } else if (name == "Write" && !info.filePath.isEmpty()) {
            isEditTool = true;
            info.isEdit = true;
            info.newString = JsonUtils::getString(input, "content",
                             JsonUtils::getString(input, "contents"));
            if (m_diffEngine)
                m_diffEngine->recordWriteToolChange(info.filePath, info.newString, t->sessionId);

            t->pendingEditFile = info.filePath;
            t->editCount++;
            emit fileChanged(info.filePath);

            if (info.filePath.contains("/.claude/plans/") && info.filePath.endsWith(".md"))
                emit planFileDetected(info.filePath);
        }

        if (name == "AskUserQuestion") {
            if (t->currentToolGroup) {
                t->currentToolGroup->finalize();
                t->currentToolGroup = nullptr;
            }
            auto *questionWidget = new QuestionWidget(input);
            if (t->messagesLayout)
                t->messagesLayout->insertWidget(insertPosForTab(*t), questionWidget);
            t->hasPendingQuestion = true;
            refreshInputBarForCurrentTab();
            connect(questionWidget, &QuestionWidget::answered, this,
                    [this, proc](const QString &response) {
                auto *tab = tabForProcess(proc);
                if (!tab) return;
                tab->hasPendingQuestion = false;

                // Send the user's answer as a regular follow-up message.
                // The CLI auto-resolves AskUserQuestion in --print mode,
                // so we resume the session with the actual user choice.
                auto doSend = [this, proc, response]() {
                    auto *t2 = tabForProcess(proc);
                    if (!t2) return;
                    t2->turnId++;
                    t2->accumulatedRawContent.clear();
                    t2->hasFirstAssistantMsg = false;
                    t2->currentAssistantMsg = nullptr;
                    t2->currentToolGroup = nullptr;
                    t2->process->setMode(m_modeSelector->currentMode());
                    t2->process->setModel(m_modelSelector->currentModelId());
                    t2->process->setProfileIds(t2->profileIds);
                    if (t2->sessionConfirmed)
                        t2->process->setSessionId(t2->sessionId);
                    setTabProcessingState(*t2, true);
                    t2->process->sendMessage(response);
                };

                if (proc->isRunning()) {
                    // Process still streaming — queue send after it finishes
                    connect(proc, &ClaudeProcess::finished, this,
                            [doSend](int) { doSend(); },
                            Qt::SingleShotConnection);
                } else {
                    doSend();
                }
            });
            scrollTabToBottom(*t);
        } else if (isEditTool) {
            if (t->currentToolGroup) {
                t->currentToolGroup->finalize();
                t->currentToolGroup = nullptr;
            }
            auto *editGroup = new ToolCallGroupWidget;
            connect(editGroup, &ToolCallGroupWidget::fileClicked, this, &ChatPanel::onToolFileClicked);
            if (t->messagesLayout)
                t->messagesLayout->insertWidget(insertPosForTab(*t), editGroup);
            editGroup->addToolCall(info);
            editGroup->finalize();
            editGroup->setExpandedByDefault();
        } else {
            if (!t->currentToolGroup) {
                t->currentToolGroup = new ToolCallGroupWidget;
                connect(t->currentToolGroup, &ToolCallGroupWidget::fileClicked, this, &ChatPanel::onToolFileClicked);
                if (t->messagesLayout)
                    t->messagesLayout->insertWidget(insertPosForTab(*t), t->currentToolGroup);
            }
            t->currentToolGroup->addToolCall(info);
        }
        scrollTabToBottom(*t);

        if (m_database) {
            MessageRecord rec;
            rec.sessionId = t->sessionId;
            rec.role = "tool";
            rec.content = info.summary;
            rec.toolName = name;
            rec.toolInput = QString::fromStdString(input.dump());
            rec.turnId = t->turnId;
            rec.timestamp = QDateTime::currentSecsSinceEpoch();
            m_database->saveMessage(rec);
        }
    });

    // Forward MCP orchestrator tool calls to the Orchestrator
    connect(proc->streamParser(), &StreamParser::toolUseStarted, this,
            [this, proc](const QString &name, const QString &, const nlohmann::json &input) {
        if (!name.startsWith("mcp__c3p2-orchestrator__")) return;
        auto *t = tabForProcess(proc);
        if (t) emit mcpOrchestratorToolCalled(t->sessionId, name, input);
    });

    connect(proc->streamParser(), &StreamParser::toolResultReceived, this,
            [this, proc](const QString &) {
        auto *t = tabForProcess(proc);
        if (!t) return;
        if (!t->pendingEditFile.isEmpty()) {
            emit fileChanged(t->pendingEditFile);
            t->pendingEditFile.clear();
        }
    });

    connect(proc->streamParser(), &StreamParser::resultReady, this,
            [this, proc](const QString &sessionId, const nlohmann::json &raw) {
        auto *t = tabForProcess(proc);
        if (!t) return;
        if (!sessionId.isEmpty() && t->sessionId != sessionId) {
            QString oldId = t->sessionId;
            t->sessionId = sessionId;
            t->sessionConfirmed = true;
            t->process->setSessionId(sessionId);
            if (m_sessionMgr)
                m_sessionMgr->updateSessionId(oldId, sessionId);
            if (m_database) {
                m_database->updateMessageSessionId(oldId, sessionId);
                m_database->deleteSession(oldId);
            }
            emit sessionIdChanged(oldId, sessionId);
            // Notify EffectsPanel and DiffEngine of the confirmed session ID
            emit activeSessionChanged(sessionId);
            // Rebuild fleet panel so cards use the confirmed session ID
            emit sessionListChanged();
        }
        // Extract context usage from result event
        if (raw.contains("usage") && raw["usage"].is_object()) {
            auto &u = raw["usage"];
            t->totalInputTokens += u.value("input_tokens", 0);
            t->totalOutputTokens += u.value("output_tokens", 0);
            t->totalCacheReadTokens += u.value("cache_read_input_tokens", 0);
        }
        if (raw.contains("total_cost_usd") && raw["total_cost_usd"].is_number())
            t->totalCostUsd += raw["total_cost_usd"].get<double>();
        updateStatsLabel();
    });

    connect(proc, &ClaudeProcess::finished, this, [this, proc](int exitCode) {
        qDebug() << "[cccpp] ChatPanel received finished(" << exitCode << ")";
        auto *t = tabForProcess(proc);
        if (!t) {
            qWarning() << "[cccpp] finished: tabForProcess returned null! Processing state will NOT be cleared.";
            return;
        }

        bool wasCancelled = (exitCode == 15 || exitCode == 9
                             || exitCode == 143 || exitCode == 137);

        flushPendingText(*t);

        if (t->currentThinkingBlock) {
            t->currentThinkingBlock->finalize();
            t->currentThinkingBlock = nullptr;
        }

        if (t->currentToolGroup) {
            t->currentToolGroup->finalize();
            t->currentToolGroup = nullptr;
        }

        if (wasCancelled) {
            if (!t->currentAssistantMsg) {
                t->currentAssistantMsg = new ChatMessageWidget(ChatMessageWidget::Assistant, "");
                t->currentAssistantMsg->setTurnId(t->turnId);
                t->currentAssistantMsg->setTimestamp(QDateTime::currentDateTime());
                if (!t->hasFirstAssistantMsg)
                    t->hasFirstAssistantMsg = true;
                else
                    t->currentAssistantMsg->setHeaderVisible(false);
                addMessageToTab(*t, t->currentAssistantMsg);
            }
            t->currentAssistantMsg->appendContent("\n\n*\\[Stopped by user\\]*");
            t->accumulatedRawContent += "\n\n*[Stopped by user]*";
        } else if (t->accumulatedRawContent.isEmpty() && exitCode != 0) {
            if (!t->currentAssistantMsg) {
                t->currentAssistantMsg = new ChatMessageWidget(ChatMessageWidget::Assistant, "");
                t->currentAssistantMsg->setTurnId(t->turnId);
                t->currentAssistantMsg->setTimestamp(QDateTime::currentDateTime());
                if (!t->hasFirstAssistantMsg)
                    t->hasFirstAssistantMsg = true;
                else
                    t->currentAssistantMsg->setHeaderVisible(false);
                addMessageToTab(*t, t->currentAssistantMsg);
            }
            t->currentAssistantMsg->appendContent(
                QStringLiteral("*(Process exited with code %1)*").arg(exitCode));
            t->accumulatedRawContent += QStringLiteral("*(Process exited with code %1)*").arg(exitCode);
        }

        saveCurrentTextSegment(*t);

        showSuggestionChips(*t, t->accumulatedRawContent);
        showAcceptAllButton(*t);
        if (t->currentAssistantMsg)
            t->currentAssistantMsg->finalizeContent();
        t->currentAssistantMsg = nullptr;

        bool hasCheckpoint = m_database &&
            !m_database->checkpointUuid(t->sessionId, t->turnId).isEmpty();
        if (hasCheckpoint && t->messagesLayout) {
            for (int i = 0; i < t->messagesLayout->count(); ++i) {
                auto *chatMsg = qobject_cast<ChatMessageWidget *>(
                    t->messagesLayout->itemAt(i)->widget());
                if (chatMsg && chatMsg->turnId() == t->turnId) {
                    chatMsg->showRevertButton(true);
                    break;
                }
            }
        }
        setTabProcessingState(*t, false);
        scrollTabToBottom(*t);
    });

    connect(proc, &ClaudeProcess::errorOccurred, this, [this, proc](const QString &err) {
        qWarning() << "[cccpp] ChatPanel received errorOccurred:" << err;
        auto *t = tabForProcess(proc);
        if (!t) {
            qWarning() << "[cccpp] errorOccurred: tabForProcess returned null!";
            return;
        }
        if (t->currentAssistantMsg)
            t->currentAssistantMsg->appendContent(
                QStringLiteral("\n\n**Error:** %1").arg(err));
        setTabProcessingState(*t, false);
    });

    connect(proc->streamParser(), &StreamParser::errorOccurred, this,
            [this, proc](const QString &err) {
        qWarning() << "[cccpp] StreamParser errorOccurred:" << err;
        auto *t = tabForProcess(proc);
        if (!t) return;
        if (t->currentAssistantMsg)
            t->currentAssistantMsg->appendContent(
                QStringLiteral("\n\n**Stream error:** %1").arg(err));
    });

    // Throttled text flusher: batches appendContent calls to ~30fps so
    // the main thread stays responsive for splitter drag events.
    auto *flushTimer = new QTimer(this);
    flushTimer->setInterval(33);
    flushTimer->setSingleShot(false);
    connect(flushTimer, &QTimer::timeout, this, [this, proc] {
        auto *t = tabForProcess(proc);
        if (t) flushPendingText(*t);
    });
    tab.textFlushTimer = flushTimer;

    // Slow-tier markdown sync: full re-render every 500ms for proper formatting
    auto *syncTimer = new QTimer(this);
    syncTimer->setInterval(500);
    syncTimer->setSingleShot(false);
    connect(syncTimer, &QTimer::timeout, this, [this, proc] {
        auto *t = tabForProcess(proc);
        if (t && t->currentAssistantMsg)
            t->currentAssistantMsg->syncMarkdown();
    });
    tab.markdownSyncTimer = syncTimer;
}

QString ChatPanel::newChat()
{
    QString sessionId;
    if (m_sessionMgr)
        sessionId = m_sessionMgr->createSession(m_workingDir, m_modeSelector->currentMode());

    ChatTab tab;
    tab.sessionId = sessionId;
    tab.updatedAt = QDateTime::currentSecsSinceEpoch();
    tab.profileIds = m_profileSelector->selectedIds();
    tab.container = createChatContent();
    tab.scrollArea = tab.container->findChild<QScrollArea *>();
    tab.messagesLayout = tab.scrollArea->widget()->findChild<QVBoxLayout *>("messagesLayout");

    tab.process = new ClaudeProcess(this);
    tab.process->setWorkingDirectory(m_workingDir);

    auto *scrollContent = tab.scrollArea->widget();

    auto *welcome = new ChatWelcomeState(scrollContent);
    welcome->setObjectName("chatWelcome");
    connect(welcome->chips(), &SuggestionChips::suggestionClicked, this, [this](const QString &text) {
        m_inputBar->setText(text);
        m_inputBar->focusInput();
    });
    tab.messagesLayout->insertWidget(0, welcome);
    tab.welcomeWidget = welcome;

    auto *indicator = new ThinkingIndicator(scrollContent);
    tab.messagesLayout->insertWidget(tab.messagesLayout->count() - 1, indicator);
    tab.thinkingIndicator = indicator;

    int idx = m_tabWidget->addTab(tab.container, "New Chat");
    tab.tabIndex = idx;
    m_tabs[idx] = tab;

    wireProcessSignals(m_tabs[idx]);

    // Scroll monitoring: detect which turn is visible
    auto *scrollBar2 = m_tabs[idx].scrollArea->verticalScrollBar();
    connect(scrollBar2, &QScrollBar::valueChanged, this, [this, idx]() {
        if (!m_tabs.contains(idx)) return;
        auto &t = m_tabs[idx];
        if (!t.messagesLayout || !t.scrollArea) return;
        int viewportTop = t.scrollArea->verticalScrollBar()->value();
        int threshold = t.scrollArea->viewport()->height() / 3;
        int bestTurnId = -1;
        for (int i = 0; i < t.messagesLayout->count(); ++i) {
            auto *item = t.messagesLayout->itemAt(i);
            if (!item || !item->widget()) continue;
            auto *chatMsg = qobject_cast<ChatMessageWidget *>(item->widget());
            if (!chatMsg || chatMsg->turnId() <= 0) continue;
            int widgetTop = chatMsg->mapTo(t.scrollArea->widget(), QPoint(0, 0)).y();
            if (widgetTop <= viewportTop + threshold)
                bestTurnId = chatMsg->turnId();
        }
        if (bestTurnId > 0)
            emit visibleTurnChanged(t.sessionId, bestTurnId);
    });

    m_tabWidget->setCurrentIndex(idx);
    emit sessionListChanged();
    return sessionId;
}

void ChatPanel::closeAllTabs()
{
    // Cancel any running processes
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->process && it->process->isRunning())
            it->process->cancel();
    }

    // Remove all tabs
    while (m_tabWidget->count() > 0)
        m_tabWidget->removeTab(0);

    m_tabs.clear();
}

// ---------------------------------------------------------------------------
// renderMessageRange — create widgets for allMessages[startIndex..endIndex)
// Returns the layout insert position after the last widget added.
// ---------------------------------------------------------------------------
int ChatPanel::renderMessageRange(ChatTab &tab, int startIndex, int endIndex, int insertPos)
{
    ToolCallGroupWidget *pendingGroup = nullptr;
    bool firstAssistantInTurn = true;
    int currentTurn = -1;
    int pos = insertPos;

    auto flushGroup = [&] {
        if (pendingGroup) {
            pendingGroup->finalize();
            pendingGroup = nullptr;
        }
    };

    for (int mi = startIndex; mi < endIndex; ++mi) {
        const auto &msg = tab.allMessages[mi];
        int turnId = msg.turnId;

        if (turnId != currentTurn) {
            flushGroup();
            firstAssistantInTurn = true;
            currentTurn = turnId;
        }

        if (msg.role == "user" && !msg.content.trimmed().isEmpty()) {
            flushGroup();
            auto *w = new ChatMessageWidget(ChatMessageWidget::User, msg.content);
            w->setTurnId(turnId);
            if (msg.timestamp > 0)
                w->setTimestamp(QDateTime::fromSecsSinceEpoch(msg.timestamp));
            w->showRevertButton(tab.checkpointTurnIds.contains(turnId));
            tab.messagesLayout->insertWidget(pos++, w);
            connect(w, &ChatMessageWidget::revertRequested, this, &ChatPanel::onRevertRequested);

        } else if (msg.role == "thinking" && !msg.content.trimmed().isEmpty()) {
            flushGroup();
            auto *tb = new ThinkingBlockWidget;
            tb->appendContent(msg.content);
            tb->finalize();
            tab.messagesLayout->insertWidget(pos++, tb);

        } else if (msg.role == "assistant" && !msg.content.trimmed().isEmpty()) {
            flushGroup();
            auto *w = new ChatMessageWidget(ChatMessageWidget::Assistant, msg.content);
            w->setTurnId(turnId);
            if (msg.timestamp > 0)
                w->setTimestamp(QDateTime::fromSecsSinceEpoch(msg.timestamp));
            if (!firstAssistantInTurn)
                w->setHeaderVisible(false);
            firstAssistantInTurn = false;
            tab.messagesLayout->insertWidget(pos++, w);
            connect(w, &ChatMessageWidget::fileNavigationRequested,
                    this, [this](const QString &fp, int line) { emit navigateToFile(fp, line); });
            connect(w, &ChatMessageWidget::applyCodeRequested,
                    this, [this](const QString &code, const QString &lang) {
                emit applyCodeRequested(code, lang, "");
            });

        } else if (msg.role == "tool") {
            ToolCallInfo info;
            info.toolName = msg.toolName;
            info.summary = msg.content;

            if (!msg.toolInput.isEmpty()) {
                auto parsed = nlohmann::json::parse(
                    msg.toolInput.toStdString(), nullptr, false);
                if (!parsed.is_discarded()) {
                    if (parsed.contains("path"))
                        info.filePath = JsonUtils::getString(parsed, "path");
                    else if (parsed.contains("file_path"))
                        info.filePath = JsonUtils::getString(parsed, "file_path");

                    if ((msg.toolName == "Edit" || msg.toolName == "StrReplace")
                        && parsed.contains("old_string")) {
                        info.isEdit = true;
                        info.oldString = JsonUtils::getString(parsed, "old_string");
                        info.newString = JsonUtils::getString(parsed, "new_string");
                    } else if (msg.toolName == "Write") {
                        info.isEdit = true;
                        info.newString = JsonUtils::getString(parsed, "content",
                                         JsonUtils::getString(parsed, "contents"));
                    }
                }
            }

            if (info.isEdit) {
                flushGroup();
                auto *editGroup = new ToolCallGroupWidget;
                connect(editGroup, &ToolCallGroupWidget::fileClicked, this, &ChatPanel::onToolFileClicked);
                editGroup->addToolCall(info);
                editGroup->finalize();
                editGroup->setExpandedByDefault();
                tab.messagesLayout->insertWidget(pos++, editGroup);
            } else {
                if (!pendingGroup) {
                    pendingGroup = new ToolCallGroupWidget;
                    connect(pendingGroup, &ToolCallGroupWidget::fileClicked, this, &ChatPanel::onToolFileClicked);
                    tab.messagesLayout->insertWidget(pos++, pendingGroup);
                }
                pendingGroup->addToolCall(info);
            }
        }
    }
    flushGroup();
    return pos;
}

// ---------------------------------------------------------------------------
// loadOlderMessages — prepend a batch of older messages, preserving scroll pos
// ---------------------------------------------------------------------------
void ChatPanel::loadOlderMessages(ChatTab &tab, int count)
{
    if (tab.lazyRenderIndex <= 0) return;
    if (tab.lazyLoadingInProgress) return;
    tab.lazyLoadingInProgress = true;

    int endIndex = tab.lazyRenderIndex;
    int startIndex = qMax(0, endIndex - count);

    // Align backward to a turn boundary so we don't split a turn
    if (startIndex > 0) {
        int turnAtStart = tab.allMessages[startIndex].turnId;
        while (startIndex > 0 && tab.allMessages[startIndex - 1].turnId == turnAtStart)
            --startIndex;
    }

    // Record scroll state before inserting
    QScrollBar *sb = tab.scrollArea->verticalScrollBar();
    int oldScrollValue = sb->value();
    QWidget *scrollContent = tab.scrollArea->widget();
    int oldContentHeight = scrollContent->sizeHint().height();

    // Render the batch at position 0 (top of layout)
    renderMessageRange(tab, startIndex, endIndex, 0);
    tab.lazyRenderIndex = startIndex;

    // Restore scroll position so viewport does not jump
    QTimer::singleShot(0, this, [sb, oldScrollValue, oldContentHeight, scrollContent] {
        int newContentHeight = scrollContent->sizeHint().height();
        int heightDelta = newContentHeight - oldContentHeight;
        sb->setValue(oldScrollValue + heightDelta);
    });

    tab.lazyLoadingInProgress = false;

    // Free stored messages when fully rendered
    if (tab.lazyRenderIndex == 0) {
        tab.allMessages.clear();
        tab.allMessages.squeeze();
        tab.checkpointTurnIds.clear();
    }
}

void ChatPanel::restoreSession(const QString &sessionId)
{
    if (!m_database) return;

    ChatTab tab;
    tab.sessionId = sessionId;
    auto sessionInfo = m_database->loadSession(sessionId);
    tab.updatedAt = sessionInfo.updatedAt;
    tab.favorite = sessionInfo.favorite;
    if (tab.updatedAt == 0)
        tab.updatedAt = QDateTime::currentSecsSinceEpoch();
    tab.container = createChatContent();
    tab.scrollArea = tab.container->findChild<QScrollArea *>();
    tab.messagesLayout = tab.scrollArea->widget()->findChild<QVBoxLayout *>("messagesLayout");

    tab.process = new ClaudeProcess(this);
    tab.process->setWorkingDirectory(m_workingDir);
    tab.process->setSessionId(sessionId);
    tab.sessionConfirmed = true;

    // Load ALL messages (needed for effects panel, editCount, title)
    tab.allMessages = m_database->loadMessages(sessionId);

    // Batch-load all checkpoints (store for lazy widget creation)
    auto checkpoints = m_database->loadCheckpoints(sessionId);
    for (const auto &cp : checkpoints) {
        if (!cp.uuid.isEmpty())
            tab.checkpointTurnIds.insert(cp.turnId);
    }

    // Compute metadata from full history (lightweight, no widget creation)
    int maxTurn = 0;
    for (const auto &msg : tab.allMessages) {
        if (msg.turnId > maxTurn) maxTurn = msg.turnId;
        if (msg.role == "tool") {
            tab.lastActivity = msg.content;
            if (msg.toolName == "Edit" || msg.toolName == "StrReplace"
                || msg.toolName == "Write" || msg.toolName == "MultiEdit") {
                tab.editCount++;
            }
        }
    }
    tab.turnId = maxTurn;

    // Determine initial render range: only the last N messages
    static constexpr int kInitialRenderCount = 40;
    int totalCount = tab.allMessages.size();
    int startIndex = qMax(0, totalCount - kInitialRenderCount);

    // Align startIndex backward to a turn boundary
    if (startIndex > 0) {
        int turnAtStart = tab.allMessages[startIndex].turnId;
        while (startIndex > 0 && tab.allMessages[startIndex - 1].turnId == turnAtStart)
            --startIndex;
    }
    tab.lazyRenderIndex = startIndex;

    // Render only the visible tail
    int insertAt = tab.messagesLayout->count() - 1; // before stretch spacer
    renderMessageRange(tab, startIndex, totalCount, insertAt);

    auto *scrollContent = tab.scrollArea->widget();
    auto *indicator = new ThinkingIndicator(scrollContent);
    tab.messagesLayout->insertWidget(tab.messagesLayout->count() - 1, indicator);
    tab.thinkingIndicator = indicator;

    // Title derivation (iterates allMessages, lightweight)
    SessionInfo info = m_sessionMgr ? m_sessionMgr->sessionInfo(sessionId) : SessionInfo();
    QString title;
    if (!info.title.isEmpty()) {
        title = info.title;
    } else {
        for (const auto &msg : tab.allMessages) {
            if (msg.role == "user" && !msg.content.trimmed().isEmpty()) {
                QString simplified = msg.content.simplified();
                if (simplified.length() <= 30) {
                    title = simplified;
                } else {
                    int cutoff = simplified.lastIndexOf(' ', 30);
                    if (cutoff < 15) cutoff = 30;
                    title = simplified.left(cutoff) + "\xe2\x80\xa6";
                }
                break;
            }
        }
        if (title.isEmpty())
            title = QStringLiteral("Chat");
    }
    if (m_sessionMgr)
        m_sessionMgr->setSessionTitle(sessionId, title);
    int idx = m_tabWidget->addTab(tab.container, title);
    tab.tabIndex = idx;
    m_tabs[idx] = tab;

    wireProcessSignals(m_tabs[idx]);

    // Scroll monitoring: lazy load trigger + visible turn detection
    auto *scrollBar = m_tabs[idx].scrollArea->verticalScrollBar();
    connect(scrollBar, &QScrollBar::valueChanged, this, [this, idx]() {
        if (!m_tabs.contains(idx)) return;
        auto &t = m_tabs[idx];
        if (!t.messagesLayout || !t.scrollArea) return;

        // Lazy load trigger: render older messages when near top
        if (t.lazyRenderIndex > 0 && !t.lazyLoadingInProgress) {
            int scrollValue = t.scrollArea->verticalScrollBar()->value();
            int viewportHeight = t.scrollArea->viewport()->height();
            if (scrollValue < viewportHeight * 1.5) {
                loadOlderMessages(t, 30);
            }
        }

        // Flush deferred resizes for widgets now in viewport
        for (int i = 0; i < t.messagesLayout->count(); ++i) {
            auto *item = t.messagesLayout->itemAt(i);
            if (!item || !item->widget()) continue;
            auto *chatMsg = qobject_cast<ChatMessageWidget *>(item->widget());
            if (chatMsg && chatMsg->needsResize())
                chatMsg->flushDeferredResize();
        }

        // Debounced visible turn detection
        m_scrollDebounce->disconnect();
        connect(m_scrollDebounce, &QTimer::timeout, this, [this, idx]() {
            if (!m_tabs.contains(idx)) return;
            auto &t = m_tabs[idx];
            if (!t.messagesLayout || !t.scrollArea) return;
            int viewportTop = t.scrollArea->verticalScrollBar()->value();
            int viewportBottom = viewportTop + t.scrollArea->viewport()->height();
            int threshold = t.scrollArea->viewport()->height() / 3;
            int bestTurnId = -1;
            for (int i = 0; i < t.messagesLayout->count(); ++i) {
                auto *item = t.messagesLayout->itemAt(i);
                if (!item || !item->widget()) continue;
                auto *chatMsg = qobject_cast<ChatMessageWidget *>(item->widget());
                if (!chatMsg || chatMsg->turnId() <= 0) continue;
                int widgetTop = chatMsg->mapTo(t.scrollArea->widget(), QPoint(0, 0)).y();
                if (widgetTop <= viewportTop + threshold)
                    bestTurnId = chatMsg->turnId();
                else if (widgetTop > viewportBottom)
                    break;
            }
            if (bestTurnId > 0)
                emit visibleTurnChanged(t.sessionId, bestTurnId);
        });
        m_scrollDebounce->start();
    });

    m_tabWidget->setCurrentIndex(idx);
    scrollTabToBottom(m_tabs[idx]);

    emit activeSessionChanged(sessionId);
    emit sessionListChanged();

    // Populate effects panel with complete history (uses stored allMessages)
    auto &storedTab = m_tabs[idx];
    auto historicalChanges = extractFileChangesFromHistory(sessionId, storedTab.allMessages);
    if (!historicalChanges.isEmpty())
        emit historicalEffectsReady(sessionId, historicalChanges);

    auto timestamps = turnTimestampsForSession(storedTab.allMessages);
    if (!timestamps.isEmpty())
        emit turnTimestampsReady(sessionId, timestamps);

    // Free stored messages if everything was rendered (short session)
    if (storedTab.lazyRenderIndex == 0) {
        storedTab.allMessages.clear();
        storedTab.allMessages.squeeze();
        storedTab.checkpointTurnIds.clear();
    }
}

void ChatPanel::sendMessage(const QString &text)
{
    onSendRequested(text);
}

void ChatPanel::onSendRequested(const QString &text)
{
    // If orchestrator toggle is ON, route through the Orchestrator
    if (m_orchestratorToggle->isChecked()) {
        m_orchestratorToggle->setChecked(false);  // one-shot: reset after use

        // Derive title from user's goal before handing off to orchestrator
        auto &tab = currentTab();
        if (tab.turnId == 0) {
            QString simplified = text.simplified();
            QString title;
            if (simplified.length() <= 30) {
                title = simplified;
            } else {
                int cutoff = simplified.lastIndexOf(' ', 30);
                if (cutoff < 15) cutoff = 30;
                title = simplified.left(cutoff) + "\xe2\x80\xa6";
            }
            m_tabWidget->setTabText(tab.tabIndex, title);
            if (m_sessionMgr)
                m_sessionMgr->setSessionTitle(tab.sessionId, title);
            emit sessionListChanged();
        }

        emit orchestrateRequested(text, m_profileSelector->selectedIds());
        return;
    }

    if (m_tabs.isEmpty())
        newChat();

    auto &tab = currentTab();

    // Hide previous suggestion chips
    if (tab.suggestionChips) {
        tab.suggestionChips->clear();
        tab.suggestionChips = nullptr;
    }

    tab.turnId++;
    tab.updatedAt = QDateTime::currentSecsSinceEpoch();
    emit turnStarted(tab.sessionId, tab.turnId);
    tab.accumulatedRawContent.clear();
    tab.hasFirstAssistantMsg = false;
    tab.currentAssistantMsg = nullptr;
    tab.currentToolGroup = nullptr;
    emit aboutToSendMessage();

    auto *userMsg = new ChatMessageWidget(ChatMessageWidget::User, text);
    userMsg->setTurnId(tab.turnId);
    userMsg->setTimestamp(QDateTime::currentDateTime());

    auto attachedImgs = m_inputBar->attachedImages();
    if (!attachedImgs.isEmpty()) {
        QList<QByteArray> imageDataForDisplay;
        for (const auto &img : attachedImgs)
            imageDataForDisplay.append(img.data);
        userMsg->setImages(imageDataForDisplay);
    }

    addMessageToTab(tab, userMsg);

    if (tab.turnId == 1) {
        QString simplified = text.simplified();
        QString title;
        if (simplified.length() <= 30) {
            title = simplified;
        } else {
            int cutoff = simplified.lastIndexOf(' ', 30);
            if (cutoff < 15) cutoff = 30;
            title = simplified.left(cutoff) + "\xe2\x80\xa6";
        }
        m_tabWidget->setTabText(tab.tabIndex, title);
        if (m_sessionMgr)
            m_sessionMgr->setSessionTitle(tab.sessionId, title);
        emit sessionListChanged();
    }

    if (m_database) {
        MessageRecord rec;
        rec.sessionId = tab.sessionId;
        rec.role = "user";
        rec.content = text;
        rec.turnId = tab.turnId;
        rec.timestamp = QDateTime::currentSecsSinceEpoch();
        m_database->saveMessage(rec);
    }

    // Build enriched message with context
    QString enrichedMessage = buildContextPreamble(text);

    tab.process->setMode(m_modeSelector->currentMode());
    tab.process->setModel(m_modelSelector->currentModelId());
    if (tab.sessionConfirmed)
        tab.process->setSessionId(tab.sessionId);

    // Inject personality profiles + workspace spec as system prompt
    QString systemPrompt = ProfileManager::instance().buildSystemPrompt(
        m_workingDir, tab.profileIds);
    tab.process->setSystemPrompt(systemPrompt);
    tab.process->setProfileIds(tab.profileIds);

    qDebug() << "[cccpp] onSendRequested: sending message, session=" << tab.sessionId;
    setTabProcessingState(tab, true);

    QList<QPair<QByteArray, QString>> imagePayload;
    for (const auto &img : attachedImgs) {
        QString mediaType = "image/" + img.format.toLower();
        imagePayload.append({img.data, mediaType});
    }
    tab.process->sendMessage(enrichedMessage, imagePayload);

    // Clear attachments after sending
    m_inputBar->clearAttachments();
    updateInputBarContext();
}

void ChatPanel::onSlashCommand(const QString &command, const QString &args)
{
    if (command == "/clear") {
        newChat();
    } else if (command == "/compact") {
        if (!m_tabs.isEmpty()) {
            sendMessage("Please provide a concise summary of our conversation so far, "
                        "then we can continue from that summary.");
        }
    } else if (command == "/help") {
        if (m_tabs.isEmpty()) newChat();
        auto &tab = currentTab();
        auto *helpMsg = new ChatMessageWidget(ChatMessageWidget::Assistant,
            "**Available commands:**\n"
            "- `/clear` - Start a new conversation\n"
            "- `/compact` - Compact conversation history\n"
            "- `/help` - Show this help\n"
            "- `/model <name>` - Switch Claude model\n"
            "- `/mode <agent|ask|plan>` - Switch mode\n"
            "- `/delegate <role> <task>` - Delegate to specialist (architect/implementer/reviewer/tester)\n"
            "- `/pipeline <name> <task>` - Run a pipeline (refactor/review)\n"
            "- `/orchestrate <goal>` - Autonomous orchestration (plan, implement, validate, review)\n\n"
            "**Shortcuts:**\n"
            "- `@` - Mention files to attach as context\n"
            "- Paste images with Ctrl/Cmd+V\n"
            "- Cmd+K in editor for inline edits");
        addMessageToTab(tab, helpMsg);
    } else if (command == "/mode" && !args.isEmpty()) {
        m_modeSelector->setMode(args.toLower());
    } else if (command == "/model" && !args.isEmpty()) {
        // Model switching handled by ModelSelector
    } else if (command == "/delegate") {
        QStringList parts = args.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            QString role = parts.takeFirst();
            QString task = parts.join(' ');
            QString profileId = "specialist-" + role.toLower();
            QString context = sessionFinalOutput(currentSessionId());
            delegateToChild(currentSessionId(), task, context, profileId);
        } else {
            if (m_tabs.isEmpty()) newChat();
            auto &tab = currentTab();
            auto *msg = new ChatMessageWidget(ChatMessageWidget::Assistant,
                "Usage: `/delegate <role> <task>`\n\n"
                "Roles: `architect`, `implementer`, `reviewer`, `tester`\n\n"
                "Example: `/delegate architect Design a REST API for user management`");
            addMessageToTab(tab, msg);
        }
    } else if (command == "/pipeline") {
        QStringList parts = args.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            QString name = parts.takeFirst();
            QString task = parts.join(' ');
            emit pipelineRequested(name, task);
        } else {
            if (m_tabs.isEmpty()) newChat();
            auto &tab = currentTab();
            auto *msg = new ChatMessageWidget(ChatMessageWidget::Assistant,
                "Usage: `/pipeline <name> <task>`\n\n"
                "Built-in pipelines: `refactor`, `review`\n\n"
                "Example: `/pipeline refactor Refactor the authentication module`");
            addMessageToTab(tab, msg);
        }
    } else if (command == "/orchestrate") {
        if (!args.trimmed().isEmpty()) {
            emit orchestrateRequested(args.trimmed(), m_profileSelector->selectedIds());
        } else {
            if (m_tabs.isEmpty()) newChat();
            auto &tab = currentTab();
            auto *msg = new ChatMessageWidget(ChatMessageWidget::Assistant,
                "Usage: `/orchestrate <goal>`\n\n"
                "Starts an autonomous orchestrator that plans, delegates to specialists, "
                "validates builds, and self-corrects until the goal is achieved.\n\n"
                "Example: `/orchestrate Build a REST API with authentication and tests`");
            addMessageToTab(tab, msg);
        }
    }
}

void ChatPanel::onToolFileClicked(const QString &filePath, const QString &searchText)
{
    QString resolved = filePath;
    if (!resolved.startsWith('/') && !m_workingDir.isEmpty())
        resolved = m_workingDir + "/" + resolved;

    int line = 0;
    if (!searchText.isEmpty()) {
        // Find the first line of the new content in the file
        QString firstLine = searchText.section('\n', 0, 0).trimmed();
        if (!firstLine.isEmpty()) {
            QFile f(resolved);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream stream(&f);
                int lineNum = 0;
                while (!stream.atEnd()) {
                    lineNum++;
                    if (stream.readLine().contains(firstLine)) {
                        line = lineNum;
                        break;
                    }
                }
            }
        }
    }
    emit navigateToFile(resolved, line);
}

void ChatPanel::onRevertRequested(int turnId)
{
    if (!m_database) return;
    int idx = m_tabWidget->currentIndex();
    if (!m_tabs.contains(idx)) return;
    const auto &tab = m_tabs[idx];

    QString uuid = m_database->checkpointUuid(tab.sessionId, turnId);
    if (uuid.isEmpty()) return;

    m_pendingRevertTurnId = turnId;
    rewindToCheckpoint(uuid);
}

void ChatPanel::rewindToCheckpoint(const QString &checkpointUuid)
{
    if (checkpointUuid.isEmpty()) return;
    int idx = m_tabWidget->currentIndex();
    if (!m_tabs.contains(idx)) return;
    auto &tab = m_tabs[idx];
    if (!tab.process || tab.sessionId.isEmpty()) return;

    tab.process->setSessionId(tab.sessionId);

    connect(tab.process, &ClaudeProcess::rewindCompleted, this,
            [this](bool success) {
        if (success && m_pendingRevertTurnId > 0)
            removeMessagesAfterTurn(m_pendingRevertTurnId);
        m_pendingRevertTurnId = 0;
        emit rewindCompleted(success);
    }, Qt::SingleShotConnection);

    tab.process->rewindFiles(checkpointUuid);
}

void ChatPanel::rewindCurrentTurn()
{
    if (!m_database) return;
    int idx = m_tabWidget->currentIndex();
    if (!m_tabs.contains(idx)) return;
    const auto &tab = m_tabs[idx];

    auto checkpoints = m_database->loadCheckpoints(tab.sessionId);
    if (!checkpoints.isEmpty())
        rewindToCheckpoint(checkpoints.last().uuid);
}

void ChatPanel::removeMessagesAfterTurn(int turnId)
{
    int idx = m_tabWidget->currentIndex();
    if (!m_tabs.contains(idx)) return;
    auto &tab = m_tabs[idx];
    if (!tab.messagesLayout) return;

    // Remove all widgets belonging to turnId and later turns.
    // Walk backwards so indices stay valid as we remove.
    for (int i = tab.messagesLayout->count() - 1; i >= 0; --i) {
        QLayoutItem *item = tab.messagesLayout->itemAt(i);
        if (!item || !item->widget()) continue;
        QWidget *w = item->widget();

        if (w == tab.thinkingIndicator || w == tab.welcomeWidget)
            continue;

        auto *chatMsg = qobject_cast<ChatMessageWidget *>(w);
        if (chatMsg) {
            if (chatMsg->turnId() >= turnId) {
                tab.messagesLayout->removeWidget(w);
                w->deleteLater();
            }
            continue;
        }

        // Non-ChatMessageWidget (ToolCallGroupWidget, SuggestionChips):
        // check if it sits after the last remaining ChatMessageWidget.
        // If so, it belonged to a removed turn — delete it.
        bool afterLastKept = true;
        for (int j = i + 1; j < tab.messagesLayout->count(); ++j) {
            QLayoutItem *next = tab.messagesLayout->itemAt(j);
            if (!next || !next->widget()) continue;
            auto *nextMsg = qobject_cast<ChatMessageWidget *>(next->widget());
            if (nextMsg && nextMsg->turnId() < turnId) {
                afterLastKept = false;
                break;
            }
        }
        if (afterLastKept) {
            tab.messagesLayout->removeWidget(w);
            w->deleteLater();
        }
    }

    tab.turnId = turnId - 1;
    tab.currentAssistantMsg = nullptr;
    tab.currentToolGroup = nullptr;
    tab.suggestionChips = nullptr;

    // Truncate lazy message state
    if (!tab.allMessages.isEmpty()) {
        auto it = std::remove_if(tab.allMessages.begin(), tab.allMessages.end(),
            [turnId](const MessageRecord &m) { return m.turnId >= turnId; });
        tab.allMessages.erase(it, tab.allMessages.end());
        tab.lazyRenderIndex = qMin(tab.lazyRenderIndex, tab.allMessages.size());
    }
}

QString ChatPanel::buildContextPreamble(const QString &userText)
{
    QStringList contextParts;
    QString processedText = userText;
    QSet<QString> resolvedPaths;

    // Auto-attached context: current file + line
    if (m_codeViewer) {
        QString currentFile = m_codeViewer->currentFile();
        if (!currentFile.isEmpty()) {
            QString relFile = currentFile;
            if (!m_workingDir.isEmpty() && currentFile.startsWith(m_workingDir))
                relFile = currentFile.mid(m_workingDir.length() + 1);
            contextParts << QStringLiteral("Currently viewing: %1").arg(relFile);
        }
    }

    // @-mentioned file contexts (from popup selection, stored as pills)
    auto contexts = m_inputBar->attachedContexts();
    for (const auto &ctx : contexts) {
        if (resolvedPaths.contains(ctx.fullPath)) continue;
        QFile file(ctx.fullPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(file.readAll());
            if (content.length() > 50000)
                content = content.left(50000) + "\n... (truncated)";
            contextParts << QStringLiteral("Content of %1:\n```\n%2\n```")
                .arg(ctx.displayName, content);
            resolvedPaths.insert(ctx.fullPath);
        }
    }

    // Fallback: resolve @filename patterns typed inline in the message text.
    // Handles patterns like @README.md, @src/main.cpp, @CMakeLists.txt
    QRegularExpression atMention("@([\\w./\\-]+\\.[\\w]+)");
    auto it = atMention.globalMatch(userText);
    while (it.hasNext()) {
        auto match = it.next();
        QString token = match.captured(1);

        // Try to resolve: first as relative to workspace, then as absolute
        QString fullPath;
        if (!m_workingDir.isEmpty()) {
            QString candidate = m_workingDir + "/" + token;
            if (QFile::exists(candidate))
                fullPath = candidate;
        }
        if (fullPath.isEmpty() && QFile::exists(token))
            fullPath = token;

        if (fullPath.isEmpty() || resolvedPaths.contains(fullPath))
            continue;

        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(file.readAll());
            if (content.length() > 50000)
                content = content.left(50000) + "\n... (truncated)";
            contextParts << QStringLiteral("Content of %1:\n```\n%2\n```")
                .arg(token, content);
            resolvedPaths.insert(fullPath);
        }
    }

    if (contextParts.isEmpty())
        return processedText;

    return contextParts.join("\n\n") + "\n\n" + processedText;
}

void ChatPanel::updateInputBarContext()
{
    if (!m_codeViewer) return;

    QString currentFile = m_codeViewer->currentFile();
    if (currentFile.isEmpty()) {
        m_inputBar->setContextIndicator("");
        return;
    }

    QString relFile = currentFile;
    if (!m_workingDir.isEmpty() && currentFile.startsWith(m_workingDir))
        relFile = currentFile.mid(m_workingDir.length() + 1);

    m_inputBar->setContextIndicator(QStringLiteral("Context: %1").arg(relFile));
}

QString ChatPanel::currentSessionId() const
{
    int idx = m_tabWidget->currentIndex();
    if (m_tabs.contains(idx))
        return m_tabs[idx].sessionId;
    return {};
}

void ChatPanel::hideTabBar()
{
    m_tabWidget->tabBar()->hide();
    // Also hide the corner widget (new chat, history, plans buttons)
    // since the fleet panel provides these now
    if (auto *corner = m_tabWidget->cornerWidget(Qt::TopRightCorner))
        corner->hide();
}

QList<AgentSummary> ChatPanel::agentSummaries() const
{
    QList<AgentSummary> result;
    QSet<QString> openIds;

    // First: currently open tabs (active agents)
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        AgentSummary s;
        s.sessionId = it->sessionId;
        int idx = it.key();
        s.title = (idx >= 0 && idx < m_tabWidget->count())
                      ? m_tabWidget->tabText(idx)
                      : it->sessionId.left(8);
        s.activity = it->lastActivity;
        s.processing = it->processing;
        s.hasPendingQuestion = it->hasPendingQuestion;
        s.unread = it->unread;
        s.editCount = it->editCount;
        s.turnCount = it->turnId;
        s.costUsd = it->totalCostUsd;
        s.profileIds = it->profileIds;
        s.updatedAt = it->updatedAt;
        s.favorite = it->favorite;
        // Hierarchy fields
        if (m_sessionMgr) {
            auto info = m_sessionMgr->sessionInfo(it->sessionId);
            s.parentSessionId = info.parentSessionId;
            s.delegationTask = info.delegationTask;
            s.pipelineId = info.pipelineId;
            s.isDelegatedChild = !info.parentSessionId.isEmpty();
            s.createdAt = info.createdAt;
        }
        result.append(s);
        openIds.insert(it->sessionId);
    }

    // Second: old sessions from database (not currently open)
    if (m_database) {
        auto sessions = m_database->loadSessions();

        // Collect IDs of closed sessions that need turn counts
        QStringList closedSessionIds;
        for (const auto &session : sessions) {
            if (session.workspace != m_workingDir) continue;
            if (openIds.contains(session.sessionId)) continue;
            closedSessionIds.append(session.sessionId);
        }

        // Single batch query for all turn counts
        auto turnCounts = m_database->turnCountsForSessions(closedSessionIds);

        for (const auto &session : sessions) {
            if (session.workspace != m_workingDir) continue;
            if (openIds.contains(session.sessionId)) continue;
            AgentSummary s;
            s.sessionId = session.sessionId;
            s.title = session.title.isEmpty()
                          ? session.sessionId.left(8) + "..."
                          : session.title;
            s.createdAt = session.createdAt;
            s.updatedAt = session.updatedAt;
            s.turnCount = turnCounts.value(session.sessionId, 0);
            s.favorite = session.favorite;
            s.parentSessionId = session.parentSessionId;
            s.delegationTask = session.delegationTask;
            s.pipelineId = session.pipelineId;
            result.append(s);
        }
    }

    return result;
}

AgentSummary ChatPanel::agentSummaryForSession(const QString &sessionId) const
{
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (it->sessionId == sessionId) {
            AgentSummary s;
            s.sessionId = it->sessionId;
            int idx = it.key();
            s.title = (idx >= 0 && idx < m_tabWidget->count())
                          ? m_tabWidget->tabText(idx)
                          : it->sessionId.left(8);
            s.activity = it->lastActivity;
            s.processing = it->processing;
            s.hasPendingQuestion = it->hasPendingQuestion;
            s.unread = it->unread;
            s.editCount = it->editCount;
            s.turnCount = it->turnId;
            s.costUsd = it->totalCostUsd;
            s.profileIds = it->profileIds;
            s.updatedAt = it->updatedAt;
            s.favorite = it->favorite;
            // Hierarchy fields from SessionManager
            if (m_sessionMgr) {
                auto info = m_sessionMgr->sessionInfo(it->sessionId);
                s.parentSessionId = info.parentSessionId;
                s.delegationTask = info.delegationTask;
                s.pipelineId = info.pipelineId;
                s.isDelegatedChild = !info.parentSessionId.isEmpty();
                s.createdAt = info.createdAt;
            }
            return s;
        }
    }
    return {};
}

void ChatPanel::selectSession(const QString &sessionId)
{
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (it->sessionId == sessionId) {
            m_tabWidget->setCurrentIndex(it.key());
            return;
        }
    }

    // Not an open tab — lazily restore from database
    restoreSession(sessionId);
}

QList<FileChange> ChatPanel::extractFileChangesFromHistory(const QString &sessionId)
{
    if (!m_database) return {};
    auto messages = m_database->loadMessages(sessionId);
    return extractFileChangesFromHistory(sessionId, messages);
}

QList<FileChange> ChatPanel::extractFileChangesFromHistory(
    const QString &sessionId, const QList<MessageRecord> &messages)
{
    QMap<QPair<int, QString>, FileChange> turnFileMap;

    for (const auto &msg : messages) {
        if (msg.role != "tool") continue;
        if (msg.toolName != "Edit" && msg.toolName != "StrReplace"
            && msg.toolName != "Write" && msg.toolName != "MultiEdit") continue;

        auto input = nlohmann::json::parse(msg.toolInput.toStdString(), nullptr, false);
        if (input.is_discarded()) continue;

        QString filePath;
        if (input.contains("path"))
            filePath = QString::fromStdString(input["path"].get<std::string>());
        else if (input.contains("file_path"))
            filePath = QString::fromStdString(input["file_path"].get<std::string>());

        if (filePath.isEmpty()) continue;

        if (!QFileInfo(filePath).isAbsolute())
            filePath = m_workingDir + "/" + filePath;

        int turnId = msg.turnId;
        auto key = qMakePair(turnId, filePath);

        FileChange change;
        change.filePath = filePath;
        change.sessionId = sessionId;
        change.turnId = turnId;
        change.type = FileChange::Modified;

        if (msg.toolName == "Write" && !turnFileMap.contains(key))
            change.type = FileChange::Created;

        if (input.contains("old_string") && input.contains("new_string")) {
            QString oldStr = QString::fromStdString(input["old_string"].get<std::string>());
            QString newStr = QString::fromStdString(input["new_string"].get<std::string>());
            int oldLines = oldStr.count('\n') + (oldStr.isEmpty() ? 0 : 1);
            int newLines = newStr.count('\n') + (newStr.isEmpty() ? 0 : 1);
            change.linesAdded = qMax(0, newLines - oldLines);
            change.linesRemoved = qMax(0, oldLines - newLines);
        } else if (input.contains("content")) {
            QString content = QString::fromStdString(input["content"].get<std::string>());
            change.linesAdded = content.count('\n') + 1;
        } else if (input.contains("contents")) {
            QString content = QString::fromStdString(input["contents"].get<std::string>());
            change.linesAdded = content.count('\n') + 1;
        }

        if (turnFileMap.contains(key)) {
            turnFileMap[key].linesAdded += change.linesAdded;
            turnFileMap[key].linesRemoved += change.linesRemoved;
        } else {
            turnFileMap[key] = change;
        }
    }

    return turnFileMap.values();
}

QMap<int, qint64> ChatPanel::turnTimestampsForSession(const QString &sessionId) const
{
    if (!m_database) return {};
    auto messages = m_database->loadMessages(sessionId);
    return turnTimestampsForSession(messages);
}

QMap<int, qint64> ChatPanel::turnTimestampsForSession(
    const QList<MessageRecord> &messages) const
{
    QMap<int, qint64> timestamps;
    for (const auto &msg : messages) {
        if (msg.turnId > 0 && msg.timestamp > 0 && !timestamps.contains(msg.turnId))
            timestamps[msg.turnId] = msg.timestamp;
    }
    return timestamps;
}

void ChatPanel::scrollToTurn(int turnId)
{
    int idx = m_tabWidget->currentIndex();
    if (!m_tabs.contains(idx)) return;
    auto &tab = m_tabs[idx];
    if (!tab.messagesLayout || !tab.scrollArea) return;

    // Find the first widget with matching turnId
    for (int i = 0; i < tab.messagesLayout->count(); ++i) {
        auto *item = tab.messagesLayout->itemAt(i);
        if (!item || !item->widget()) continue;
        auto *chatMsg = qobject_cast<ChatMessageWidget *>(item->widget());
        if (chatMsg && chatMsg->turnId() == turnId) {
            tab.scrollArea->ensureWidgetVisible(chatMsg, 0, 20);
            return;
        }
    }

    // Target turn not found — force-render older messages up to it
    if (tab.lazyRenderIndex > 0 && !tab.allMessages.isEmpty()) {
        for (int mi = 0; mi < tab.lazyRenderIndex; ++mi) {
            if (tab.allMessages[mi].turnId >= turnId) {
                loadOlderMessages(tab, tab.lazyRenderIndex - mi);
                QTimer::singleShot(20, this, [this, turnId] { scrollToTurn(turnId); });
                return;
            }
        }
    }
}

void ChatPanel::showSuggestionChips(ChatTab &tab, const QString &responseText)
{
    QStringList suggestions;

    // Heuristic: if response mentions files that were edited, suggest reviewing them
    if (responseText.contains("error") || responseText.contains("fix") ||
        responseText.contains("bug")) {
        suggestions << "Explain the fix" << "Are there similar issues?";
    }

    if (responseText.contains("TODO") || responseText.contains("next step")) {
        suggestions << "Continue" << "What's left?";
    }

    if (suggestions.isEmpty())
        return;

    // Limit to 3 suggestions
    while (suggestions.size() > 3)
        suggestions.removeLast();

    auto *chips = new SuggestionChips;
    chips->setSuggestions(suggestions);
    connect(chips, &SuggestionChips::suggestionClicked, this, [this](const QString &text) {
        sendMessage(text);
    });

    if (tab.messagesLayout)
        tab.messagesLayout->insertWidget(insertPosForTab(tab), chips);
    tab.suggestionChips = chips;
    scrollTabToBottom(tab);
}

void ChatPanel::showAcceptAllButton(ChatTab &tab)
{
    if (!m_diffEngine || m_diffEngine->changedFiles().isEmpty())
        return;
    if (!tab.messagesLayout)
        return;

    auto &thm = ThemeManager::instance();
    auto *btn = new QPushButton("Accept all edits");
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 8px; padding: 6px 16px; font-size: 12px; }"
            "QPushButton:hover { background: %4; color: %5; }")
            .arg(thm.hex("bg_raised"), thm.hex("text_secondary"),
                 thm.hex("border_standard"),
                 thm.hex("hover_raised"), thm.hex("text_primary")));

    connect(btn, &QPushButton::clicked, this, [this, btn]() {
        if (m_codeViewer)
            m_codeViewer->clearAllDiffMarkers();
        if (m_diffEngine)
            m_diffEngine->clearPendingDiffs();
        btn->deleteLater();
    });

    tab.messagesLayout->insertWidget(insertPosForTab(tab), btn);
    scrollTabToBottom(tab);
}

ChatTab &ChatPanel::currentTab()
{
    int idx = m_tabWidget->currentIndex();
    return m_tabs[idx];
}

ChatTab *ChatPanel::tabForProcess(ClaudeProcess *proc)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->process == proc)
            return &it.value();
    }
    return nullptr;
}

QWidget *ChatPanel::createChatContent()
{
    auto *container = new QWidget;
    auto *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    auto *scrollArea = new QScrollArea(container);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *scrollContent = new QWidget;
    auto *messagesLayout = new QVBoxLayout(scrollContent);
    messagesLayout->setObjectName("messagesLayout");
    messagesLayout->setContentsMargins(20, 16, 20, 16);
    messagesLayout->setSpacing(6);
    messagesLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    containerLayout->addWidget(scrollArea);

    return container;
}

int ChatPanel::insertPosForTab(const ChatTab &tab) const
{
    if (!tab.messagesLayout) return 0;
    if (tab.thinkingIndicator) {
        int idx = tab.messagesLayout->indexOf(tab.thinkingIndicator);
        if (idx >= 0) return idx;
    }
    return tab.messagesLayout->count() - 1;
}

void ChatPanel::saveCurrentTextSegment(ChatTab &tab)
{
    if (!tab.currentAssistantMsg || !m_database) return;
    QString content = tab.currentAssistantMsg->rawContent().trimmed();
    if (content.isEmpty()) return;
    MessageRecord rec;
    rec.sessionId = tab.sessionId;
    rec.role = "assistant";
    rec.content = content;
    rec.turnId = tab.turnId;
    rec.timestamp = QDateTime::currentSecsSinceEpoch();
    m_database->saveMessage(rec);
}

void ChatPanel::addMessageToTab(ChatTab &tab, ChatMessageWidget *msg)
{
    if (!tab.messagesLayout) return;

    if (tab.welcomeWidget && tab.welcomeWidget->isVisible())
        tab.welcomeWidget->hide();

    tab.messagesLayout->insertWidget(insertPosForTab(tab), msg);

    connect(msg, &ChatMessageWidget::revertRequested,
            this, &ChatPanel::onRevertRequested);
    connect(msg, &ChatMessageWidget::fileNavigationRequested,
            this, [this](const QString &filePath, int line) {
        emit navigateToFile(filePath, line);
    });
    connect(msg, &ChatMessageWidget::applyCodeRequested,
            this, [this](const QString &code, const QString &language) {
        emit applyCodeRequested(code, language, "");
    });
}

void ChatPanel::flushPendingText(ChatTab &tab)
{
    if (tab.pendingText.isEmpty()) return;
    if (!tab.currentAssistantMsg) {
        tab.pendingText.clear();
        return;
    }
    tab.currentAssistantMsg->appendContentFast(tab.pendingText);
    tab.pendingText.clear();
    scrollTabToBottom(tab);
}

void ChatPanel::scrollTabToBottom(ChatTab &tab)
{
    if (!tab.scrollArea) return;
    QTimer::singleShot(10, this, [sa = tab.scrollArea] {
        auto *sb = sa->verticalScrollBar();
        sb->setValue(sb->maximum());
    });
}

void ChatPanel::setTabProcessingState(ChatTab &tab, bool processing)
{
    qDebug() << "[cccpp] setTabProcessingState" << (processing ? "ON" : "OFF")
             << "tab=" << tab.tabIndex << "session=" << tab.sessionId;

    tab.processing = processing;

    if (processing) {
        tab.unread = false;
    } else if (tab.tabIndex != m_tabWidget->currentIndex()) {
        tab.unread = true;
    }
    updateTabIcon(tab.tabIndex);

    if (tab.thinkingIndicator) {
        if (processing)
            tab.thinkingIndicator->startAnimation();
        else
            tab.thinkingIndicator->stopAnimation();
    }

    if (tab.textFlushTimer) {
        if (processing) {
            tab.textFlushTimer->start();
        } else {
            tab.textFlushTimer->stop();
            flushPendingText(tab);
        }
    }

    if (tab.markdownSyncTimer) {
        if (processing) {
            tab.markdownSyncTimer->start();
        } else {
            tab.markdownSyncTimer->stop();
        }
    }

    if (tab.tabIndex == m_tabWidget->currentIndex())
        refreshInputBarForCurrentTab();

    // Per-session signal for orchestrator/pipeline turn detection
    if (!processing)
        emit sessionFinishedProcessing(tab.sessionId);

    bool anyProcessing = false;
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (it->processing) { anyProcessing = true; break; }
    }
    emit processingChanged(anyProcessing);
}

QIcon ChatPanel::dotIcon(const QColor &color)
{
    const int sz = 8;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(0, 0, sz, sz);
    return QIcon(pm);
}

void ChatPanel::updateTabIcon(int tabIndex)
{
    if (!m_tabs.contains(tabIndex)) return;
    const auto &tab = m_tabs[tabIndex];
    auto &thm = ThemeManager::instance();

    if (tab.processing) {
        m_tabWidget->setTabIcon(tabIndex, dotIcon(QColor(thm.hex("teal"))));
    } else if (tab.unread && tabIndex != m_tabWidget->currentIndex()) {
        m_tabWidget->setTabIcon(tabIndex, dotIcon(QColor(thm.hex("teal"))));
    } else {
        m_tabWidget->setTabIcon(tabIndex, QIcon());
    }
}

void ChatPanel::refreshInputBarForCurrentTab()
{
    int idx = m_tabWidget->currentIndex();
    bool busy = m_tabs.contains(idx) && m_tabs[idx].processing;
    bool pendingQ = m_tabs.contains(idx) && m_tabs[idx].hasPendingQuestion;

    if (busy) {
        m_inputBar->setProcessing(true);
        m_inputBar->setPlaceholder("Claude is thinking...");
    } else if (pendingQ) {
        // Pending question with no active process: disable input without
        // showing the stop button so the user answers via the QuestionWidget.
        m_inputBar->setProcessing(false);
        m_inputBar->setEnabled(false);
        m_inputBar->setPlaceholder("Answer the question above to continue...");
    } else {
        m_inputBar->setProcessing(false);
        m_inputBar->setEnabled(true);
        m_inputBar->setPlaceholder("Ask Claude anything... (@ to mention files, / for commands)");
    }
    updateInputBarContext();

    // Sync profile selector with current tab
    if (m_tabs.contains(idx))
        m_profileSelector->setSelectedIds(m_tabs[idx].profileIds);
}

QString ChatPanel::buildInlineDiffHtml(const QString &filePath, const QString &oldStr, const QString &newStr)
{
    QString html;
    QFileInfo fi(filePath);

    int editLine = 0;
    if (!oldStr.isEmpty()) {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString contents = QString::fromUtf8(f.readAll());
            int pos = contents.indexOf(oldStr);
            if (pos >= 0)
                editLine = contents.left(pos).count('\n') + 1;
        }
    }

    auto &thm = ThemeManager::instance();

    html += QStringLiteral(
        "<br><table cellspacing='0' cellpadding='0' style='width:100%%;margin:4px 0;'><tr><td>"
        "<table cellspacing='0' cellpadding='0' style='width:100%%;background:%1;border:1px solid %2;'>"
        "<tr><td style='background:%3;padding:6px 10px;border-bottom:1px solid %2;'>"
        "<a href='cccpp://open?file=%4&line=%5' style='color:%6;text-decoration:none;"
        "font-family:\"JetBrains Mono\";font-size:12px;'>%7</a></td></tr>")
        .arg(thm.hex("bg_base"), thm.hex("border_standard"), thm.hex("bg_surface"),
             filePath.toHtmlEscaped(), QString::number(editLine),
             thm.hex("teal"), fi.fileName().toHtmlEscaped());

    html += QStringLiteral("<tr><td style='padding:4px 0;font-family:\"JetBrains Mono\";font-size:12px;'>");

    if (!oldStr.isEmpty()) {
        QStringList oldLines = oldStr.split('\n');
        int maxLines = qMin(oldLines.size(), 8);
        for (int i = 0; i < maxLines; ++i)
            html += QStringLiteral(
                "<div style='background:%2;color:%3;padding:1px 10px;white-space:pre;'>-%1</div>")
                .arg(oldLines[i].toHtmlEscaped(), thm.hex("diff_del_bg"), thm.hex("red"));
        if (oldLines.size() > maxLines)
            html += QStringLiteral(
                "<div style='color:%2;padding:1px 10px;font-size:11px;'>... %1 more lines</div>")
                .arg(oldLines.size() - maxLines).arg(thm.hex("text_faint"));
    }

    if (!newStr.isEmpty()) {
        QStringList newLines = newStr.split('\n');
        int maxLines = qMin(newLines.size(), 8);
        for (int i = 0; i < maxLines; ++i)
            html += QStringLiteral(
                "<div style='background:%2;color:%3;padding:1px 10px;white-space:pre;'>+%1</div>")
                .arg(newLines[i].toHtmlEscaped(), thm.hex("diff_add_bg"), thm.hex("green"));
        if (newLines.size() > maxLines)
            html += QStringLiteral(
                "<div style='color:%2;padding:1px 10px;font-size:11px;'>... %1 more lines</div>")
                .arg(newLines.size() - maxLines).arg(thm.hex("text_faint"));
    }

    html += "</td></tr></table></td></tr></table>";
    return html;
}

QString ChatPanel::buildDiffMarkdown(const QString &filePath, const QString &oldStr, const QString &newStr)
{
    QString md;
    int maxLines = 8;

    auto sanitize = [](const QString &line) {
        QString s = line;
        s.replace("```", "` ` `");
        return s;
    };

    md += "\n```diff\n";
    md += "+++ " + filePath + "\n";

    if (!oldStr.isEmpty()) {
        QStringList lines = oldStr.split('\n');
        int show = qMin(lines.size(), maxLines);
        for (int i = 0; i < show; ++i)
            md += "-" + sanitize(lines[i]) + "\n";
        if (lines.size() > maxLines)
            md += "... " + QString::number(lines.size() - maxLines) + " more lines\n";
    }

    if (!newStr.isEmpty()) {
        QStringList lines = newStr.split('\n');
        int show = qMin(lines.size(), maxLines);
        for (int i = 0; i < show; ++i)
            md += "+" + sanitize(lines[i]) + "\n";
        if (lines.size() > maxLines)
            md += "... " + QString::number(lines.size() - maxLines) + " more lines\n";
    }

    md += "```\n";
    return md;
}

void ChatPanel::exportChatHistory(const QString &sessionId)
{
    if (!m_database) return;

    SessionInfo info = m_sessionMgr ? m_sessionMgr->sessionInfo(sessionId) : SessionInfo();
    auto messages = m_database->loadMessages(sessionId);

    nlohmann::json root;
    root["session_id"] = sessionId.toStdString();
    root["title"] = info.title.toStdString();
    root["workspace"] = info.workspace.toStdString();
    root["mode"] = info.mode.toStdString();
    root["created_at"] = info.createdAt;
    root["updated_at"] = info.updatedAt;

    nlohmann::json msgs = nlohmann::json::array();
    for (const auto &m : messages) {
        nlohmann::json obj;
        obj["id"] = m.id;
        obj["role"] = m.role.toStdString();
        obj["content"] = m.content.toStdString();
        if (!m.toolName.isEmpty())
            obj["tool_name"] = m.toolName.toStdString();
        if (!m.toolInput.isEmpty()) {
            auto parsed = nlohmann::json::parse(m.toolInput.toStdString(),
                                                nullptr, false);
            obj["tool_input"] = parsed.is_discarded()
                ? nlohmann::json(m.toolInput.toStdString())
                : parsed;
        }
        obj["turn_id"] = m.turnId;
        obj["timestamp"] = m.timestamp;
        msgs.push_back(obj);
    }
    root["messages"] = msgs;

    QString defaultName = info.title.isEmpty()
        ? sessionId.left(8) : info.title;
    defaultName.replace(QRegularExpression("[^\\w\\- ]"), "_");

    QString path = QFileDialog::getSaveFileName(
        this, "Export Chat History",
        QDir::homePath() + "/" + defaultName + ".json",
        "JSON Files (*.json)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Failed",
                             "Could not write to " + path);
        return;
    }
    file.write(QByteArray::fromStdString(root.dump(2)));
    file.close();
}

void ChatPanel::deleteSession(const QString &sessionId)
{
    auto answer = QMessageBox::question(
        this, "Delete Chat",
        "Permanently delete this chat and all its messages?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    deleteSessionNoConfirm(sessionId);
}

void ChatPanel::deleteSessionNoConfirm(const QString &sessionId)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->sessionId == sessionId) {
            if (it->process && it->process->isRunning())
                it->process->cancel();
            int idx = it->tabIndex;
            m_tabs.erase(it);
            m_tabWidget->removeTab(idx);

            QMap<int, ChatTab> reindexed;
            int i = 0;
            for (auto jt = m_tabs.begin(); jt != m_tabs.end(); ++jt, ++i) {
                jt.value().tabIndex = i;
                reindexed[i] = jt.value();
            }
            m_tabs = reindexed;
            break;
        }
    }

    if (m_database)
        m_database->deleteSession(sessionId);
    if (m_sessionMgr)
        m_sessionMgr->removeSession(sessionId);
}

void ChatPanel::setSessionFavorite(const QString &sessionId, bool favorite)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->sessionId == sessionId) {
            it->favorite = favorite;
            return;
        }
    }
}

void ChatPanel::renameSession(const QString &sessionId, const QString &title)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->sessionId == sessionId) {
            m_tabWidget->setTabText(it.key(), title);
            return;
        }
    }
}

void ChatPanel::showHistoryMenu()
{
    if (!m_database) return;

    auto sessions = m_database->loadSessions();
    if (sessions.isEmpty()) return;

    QMenu menu(this);
    auto &thm = ThemeManager::instance();

    QSet<QString> openIds;
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it)
        openIds.insert(it->sessionId);

    int count = 0;
    for (const auto &session : sessions) {
        if (session.workspace != m_workingDir) continue;
        if (openIds.contains(session.sessionId)) continue;

        QString label = session.title;
        if (label.isEmpty())
            label = session.sessionId.left(8) + "...";

        QDateTime dt = QDateTime::fromSecsSinceEpoch(session.updatedAt);
        label += "  " + dt.toString("MMM d, hh:mm");

        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(8, 2, 4, 2);
        layout->setSpacing(4);

        auto *nameBtn = new QPushButton(label, row);
        nameBtn->setFlat(true);
        nameBtn->setCursor(Qt::PointingHandCursor);
        nameBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        nameBtn->setStyleSheet(
            QStringLiteral(
                "QPushButton { text-align: left; padding: 4px 4px; border: none;"
                " color: %1; font-size: 12px; }"
                "QPushButton:hover { color: %2; }")
            .arg(thm.hex("text_primary"), thm.hex("teal")));

        auto actionBtnStyle = QStringLiteral(
            "QPushButton { border: 1px solid %1; font-size: 11px;"
            " border-radius: 4px; background: %2; padding: 2px 6px; color: %5; }"
            "QPushButton:hover { background: %3; border-color: %4; }");

        auto *exportBtn = new QPushButton("Export", row);
        exportBtn->setCursor(Qt::PointingHandCursor);
        exportBtn->setStyleSheet(actionBtnStyle
            .arg(thm.hex("border_standard"), thm.hex("bg_base"),
                 thm.hex("bg_surface"), thm.hex("teal"),
                 thm.hex("text_muted")));

        auto *delBtn = new QPushButton("Delete", row);
        delBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setStyleSheet(actionBtnStyle
            .arg(thm.hex("border_standard"), thm.hex("bg_base"),
                 thm.hex("bg_surface"), thm.hex("red"),
                 thm.hex("text_muted")));

        layout->addWidget(nameBtn, 1);
        layout->addWidget(exportBtn);
        layout->addWidget(delBtn);

        auto *widgetAction = new QWidgetAction(&menu);
        widgetAction->setDefaultWidget(row);
        menu.addAction(widgetAction);

        QString sid = session.sessionId;
        connect(nameBtn, &QPushButton::clicked, this, [this, &menu, sid] {
            menu.close();
            restoreSession(sid);
        });
        connect(exportBtn, &QPushButton::clicked, this, [this, &menu, sid] {
            menu.close();
            exportChatHistory(sid);
        });
        connect(delBtn, &QPushButton::clicked, this, [this, &menu, sid] {
            menu.close();
            deleteSession(sid);
        });

        if (++count >= 20) break;
    }

    if (count == 0) {
        menu.addAction("No previous chats")->setEnabled(false);
    }

    menu.exec(m_historyBtn->mapToGlobal(QPoint(0, m_historyBtn->height())));
}

void ChatPanel::showPlansMenu()
{
    QString plansDir = QDir::homePath() + "/.claude/plans";
    QDir dir(plansDir);
    QFileInfoList entries = dir.entryInfoList({"*.md"}, QDir::Files, QDir::Time);

    QMenu menu(this);
    if (entries.isEmpty()) {
        menu.addAction("No plans yet")->setEnabled(false);
    } else {
        for (const auto &fi : entries) {
            QString label = fi.baseName();
            QDateTime mod = fi.lastModified();
            label += "  " + mod.toString("MMM d, hh:mm");

            auto *action = menu.addAction(label);
            QString path = fi.absoluteFilePath();
            connect(action, &QAction::triggered, this, [this, path] {
                emit planFileDetected(path);
            });
        }
    }
    menu.exec(m_plansBtn->mapToGlobal(QPoint(0, m_plansBtn->height())));
}

static QString formatTokenCount(int tokens)
{
    if (tokens >= 1000000)
        return QString::number(tokens / 1000000.0, 'f', 1) + "M";
    if (tokens >= 1000)
        return QString::number(tokens / 1000.0, 'f', 1) + "k";
    return QString::number(tokens);
}

// ─── Delegation API ──────────────────────────────────────────────────────────

QString ChatPanel::delegateToChild(const QString &parentSessionId,
                                    const QString &task,
                                    const QString &context,
                                    const QString &specialistProfileId,
                                    const QStringList &extraProfileIds,
                                    const QString &agentName,
                                    const QString &teamId,
                                    const QStringList &teammates)
{
    // Determine mode and profile from specialist
    QString mode = "agent";
    QStringList profileIds;
    if (!specialistProfileId.isEmpty()) {
        auto prof = ProfileManager::instance().profile(specialistProfileId);
        if (!prof.id.isEmpty()) {
            if (!prof.enforcedMode.isEmpty())
                mode = prof.enforcedMode;
            profileIds << specialistProfileId;
        }
    }
    // Append user's personality/domain profiles (e.g. vue3-expert, postgres-expert)
    for (const auto &id : extraProfileIds) {
        if (!profileIds.contains(id))
            profileIds << id;
    }

    // Create child session
    if (!m_sessionMgr) return {};
    QString childId = m_sessionMgr->createChildSession(parentSessionId, m_workingDir, mode, task);

    // Build ChatTab
    ChatTab tab;
    tab.sessionId = childId;
    tab.updatedAt = QDateTime::currentSecsSinceEpoch();
    tab.profileIds = profileIds;
    tab.container = createChatContent();
    tab.scrollArea = tab.container->findChild<QScrollArea *>();
    tab.messagesLayout = tab.scrollArea->widget()->findChild<QVBoxLayout *>("messagesLayout");

    tab.process = new ClaudeProcess(this);
    tab.process->setWorkingDirectory(m_workingDir);
    tab.process->setMode(mode);

    // Set agent identity for peer messaging
    if (!agentName.isEmpty())
        tab.process->setAgentName(agentName);
    if (!teamId.isEmpty())
        tab.process->setTeamId(teamId);

    auto *scrollContent = tab.scrollArea->widget();
    auto *indicator = new ThinkingIndicator(scrollContent);
    tab.messagesLayout->insertWidget(tab.messagesLayout->count() - 1, indicator);
    tab.thinkingIndicator = indicator;

    // Build a descriptive title: "Role: task summary..."
    QString roleName;
    if (!specialistProfileId.isEmpty()) {
        auto prof = ProfileManager::instance().profile(specialistProfileId);
        if (!prof.id.isEmpty()) roleName = prof.name;
    }
    QString shortTask = task.left(20) + (task.length() > 20 ? QStringLiteral("\u2026") : QString());
    QString shortTitle = roleName.isEmpty() ? shortTask : QStringLiteral("%1: %2").arg(roleName, shortTask);
    int idx = m_tabWidget->addTab(tab.container, shortTitle);
    // Persist the title so restoreSession() doesn't derive from "## Context from parent session"
    if (m_sessionMgr)
        m_sessionMgr->setSessionTitle(childId, shortTitle);
    tab.tabIndex = idx;
    m_tabs[idx] = tab;
    wireProcessSignals(m_tabs[idx]);

    // Wire completion: when child finishes, extract output and emit.
    // Use LIVE session IDs (not captured values) because Claude CLI
    // assigns its own IDs, which updates both child and parent tabs.
    auto *proc = tab.process;
    connect(proc, &ClaudeProcess::finished, this,
            [this, proc](int) {
        auto *t = tabForProcess(proc);
        if (!t) return;
        flushPendingText(*t);
        saveCurrentTextSegment(*t);
        if (t->currentAssistantMsg)
            t->currentAssistantMsg->finalizeContent();
        QString currentChildId = t->sessionId;
        // Look up the LIVE parent session ID from SessionManager
        QString liveParentId;
        if (m_sessionMgr) {
            auto info = m_sessionMgr->sessionInfo(currentChildId);
            liveParentId = info.parentSessionId;
            m_sessionMgr->setDelegationStatus(currentChildId, SessionInfo::Completed);
        }
        QString output = sessionFinalOutput(currentChildId);
        if (m_sessionMgr)
            m_sessionMgr->setDelegationResult(currentChildId, output);
        emit childSessionCompleted(liveParentId, currentChildId, output);
    });

    // Build and send message
    QString message = task;
    if (!context.isEmpty()) {
        message = QStringLiteral(
            "## Context from parent session\n%1\n\n## Your Task\n%2")
            .arg(context, task);
    }

    // Create user message widget
    tab.turnId++;
    tab.updatedAt = QDateTime::currentSecsSinceEpoch();
    auto *userMsg = new ChatMessageWidget(ChatMessageWidget::User, message);
    userMsg->setTurnId(tab.turnId);
    userMsg->setTimestamp(QDateTime::currentDateTime());
    addMessageToTab(m_tabs[idx], userMsg);

    // Save to DB
    if (m_database) {
        MessageRecord rec;
        rec.sessionId = childId;
        rec.role = "user";
        rec.content = message;
        rec.turnId = tab.turnId;
        rec.timestamp = QDateTime::currentSecsSinceEpoch();
        m_database->saveMessage(rec);
    }

    // Configure and send
    QString systemPrompt = ProfileManager::instance().buildSystemPrompt(m_workingDir, profileIds);

    // Add peer messaging instructions when part of a team
    if (!teammates.isEmpty() && !agentName.isEmpty()) {
        systemPrompt += QStringLiteral(
            "\n\n## Peer Communication\n"
            "Your agent name is '%1'. Your teammates are: %2.\n\n"
            "You have two messaging tools:\n"
            "- `send_message(to, content)` — send a message to a specific teammate\n"
            "- `check_inbox()` — check for messages from teammates\n\n"
            "Use messaging to coordinate when your work depends on or affects a teammate's work. "
            "After each major step, check your inbox for feedback. "
            "Share key decisions (API shapes, file paths, interfaces) proactively.")
            .arg(agentName, teammates.join(", "));
    }

    m_tabs[idx].process->setSystemPrompt(systemPrompt);
    m_tabs[idx].process->setProfileIds(profileIds);
    m_tabs[idx].process->setModel(m_modelSelector->currentModelId());

    m_sessionMgr->setDelegationStatus(childId, SessionInfo::Running);
    setTabProcessingState(m_tabs[idx], true);
    m_tabs[idx].process->sendMessage(message);

    emit sessionListChanged();
    return childId;
}

void ChatPanel::sendMessageToSession(const QString &sessionId, const QString &text)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->sessionId != sessionId) continue;
        auto &tab = it.value();

        tab.turnId++;
        tab.updatedAt = QDateTime::currentSecsSinceEpoch();
        emit turnStarted(tab.sessionId, tab.turnId);
        tab.accumulatedRawContent.clear();
        tab.hasFirstAssistantMsg = false;
        tab.currentAssistantMsg = nullptr;
        tab.currentToolGroup = nullptr;

        auto *userMsg = new ChatMessageWidget(ChatMessageWidget::User, text);
        userMsg->setTurnId(tab.turnId);
        userMsg->setTimestamp(QDateTime::currentDateTime());
        addMessageToTab(tab, userMsg);

        if (m_database) {
            MessageRecord rec;
            rec.sessionId = tab.sessionId;
            rec.role = "user";
            rec.content = text;
            rec.turnId = tab.turnId;
            rec.timestamp = QDateTime::currentSecsSinceEpoch();
            m_database->saveMessage(rec);
        }

        // Apply process configuration (mode, model, profiles, system prompt)
        if (!tab.overrideMode.isEmpty())
            tab.process->setMode(tab.overrideMode);
        else
            tab.process->setMode(m_modeSelector->currentMode());
        tab.process->setModel(m_modelSelector->currentModelId());
        if (tab.sessionConfirmed)
            tab.process->setSessionId(tab.sessionId);

        QString systemPrompt = ProfileManager::instance().buildSystemPrompt(
            m_workingDir, tab.profileIds);
        tab.process->setSystemPrompt(systemPrompt);
        tab.process->setProfileIds(tab.profileIds);

        setTabProcessingState(tab, true);
        tab.process->sendMessage(text);
        return;
    }
}

void ChatPanel::configureSession(const QString &sessionId,
                                  const QString &mode,
                                  const QStringList &profileIds)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->sessionId != sessionId) continue;
        it->overrideMode = mode;
        it->profileIds = profileIds;
        return;
    }
}

QString ChatPanel::sessionFinalOutput(const QString &sessionId) const
{
    if (!m_database) return {};
    auto messages = m_database->loadMessages(sessionId);
    QString lastAssistantContent;
    for (const auto &msg : messages) {
        if (msg.role == "assistant")
            lastAssistantContent = msg.content;
    }
    if (lastAssistantContent.length() > 4000)
        lastAssistantContent = lastAssistantContent.left(4000) + "\n\n[... truncated ...]";
    return lastAssistantContent;
}

void ChatPanel::updateStatsLabel()
{
    int idx = m_tabWidget->currentIndex();
    if (!m_tabs.contains(idx)) {
        m_statsLabel->hide();
        return;
    }
    const auto &tab = m_tabs[idx];
    int totalIn = tab.totalInputTokens + tab.totalCacheReadTokens;
    if (totalIn == 0 && tab.totalOutputTokens == 0) {
        m_statsLabel->hide();
        return;
    }

    QString text = QString::fromUtf8("\xe2\x86\x91 %1  \xe2\x86\x93 %2")
        .arg(formatTokenCount(totalIn), formatTokenCount(tab.totalOutputTokens));
    if (tab.totalCacheReadTokens > 0)
        text += QString("  cache %1").arg(formatTokenCount(tab.totalCacheReadTokens));
    if (tab.totalCostUsd > 0.0)
        text += QString("  $%1").arg(QString::number(tab.totalCostUsd, 'f', 4));
    m_statsLabel->setText(text);
    m_statsLabel->show();
}
