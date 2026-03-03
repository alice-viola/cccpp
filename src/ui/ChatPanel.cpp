#include "ui/ChatPanel.h"
#include "ui/InputBar.h"
#include "ui/ModeSelector.h"
#include "ui/ModelSelector.h"
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

ChatPanel::ChatPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

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

    m_statsLabel = new QLabel(this);
    m_statsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statsLabel->setFixedHeight(18);
    m_statsLabel->setContentsMargins(0, 0, 8, 0);
    m_statsLabel->setStyleSheet("QLabel { color: #6c7086; font-size: 10px; }");
    m_statsLabel->hide();
    mainLayout->addWidget(m_statsLabel);

    m_inputBar = new InputBar(this);
    m_inputBar->addFooterWidget(m_modeSelector);
    m_inputBar->addFooterWidget(m_modelSelector);
    mainLayout->addWidget(m_inputBar);

    connect(m_inputBar, &InputBar::sendRequested, this, &ChatPanel::onSendRequested);
    connect(m_inputBar, &InputBar::stopRequested, this, [this] {
        int idx = m_tabWidget->currentIndex();
        if (m_tabs.contains(idx) && m_tabs[idx].processing) {
            m_tabs[idx].process->cancel();
        }
    });
    connect(m_inputBar, &InputBar::slashCommand, this, &ChatPanel::onSlashCommand);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int idx) {
        refreshInputBarForCurrentTab();
        updateStatsLabel();
        if (m_tabs.contains(idx)) {
            m_tabs[idx].unread = false;
            emit activeSessionChanged(m_tabs[idx].sessionId);
        }
        // Update all tab icons — the old tab may now need a processing/unread dot
        for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it)
            updateTabIcon(it.key());
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
        emit agentActivityChanged(t->sessionId, info.summary);

        bool isEditTool = false;

        if ((name == "Edit" || name == "StrReplace") && input.contains("old_string")) {
            isEditTool = true;
            info.isEdit = true;
            info.oldString = JsonUtils::getString(input, "old_string");
            info.newString = JsonUtils::getString(input, "new_string");

            if (m_diffEngine)
                m_diffEngine->recordEditToolChange(info.filePath, info.oldString, info.newString);
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
                m_diffEngine->recordWriteToolChange(info.filePath, info.newString);

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
}

QString ChatPanel::newChat()
{
    QString sessionId;
    if (m_sessionMgr)
        sessionId = m_sessionMgr->createSession(m_workingDir, m_modeSelector->currentMode());

    ChatTab tab;
    tab.sessionId = sessionId;
    tab.container = createChatContent();
    tab.scrollArea = tab.container->findChild<QScrollArea *>();
    tab.messagesLayout = tab.scrollArea->widget()->findChild<QVBoxLayout *>("messagesLayout");

    tab.process = new ClaudeProcess(this);
    tab.process->setWorkingDirectory(m_workingDir);

    auto *scrollContent = tab.scrollArea->widget();

    auto *welcome = new QWidget(scrollContent);
    welcome->setObjectName("chatWelcome");
    welcome->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *welcomeLayout = new QVBoxLayout(welcome);
    welcomeLayout->setAlignment(Qt::AlignCenter);
    welcomeLayout->setSpacing(12);

    auto &thm2 = ThemeManager::instance();
    auto *welcomeLabel = new QLabel(welcome);
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setText(
        QStringLiteral(
        "<div style='color:%1;font-size:32px;margin-bottom:16px;'>&#x2726;</div>"
        "<div style='color:%2;font-size:14px;font-weight:500;"
        "margin-bottom:6px;'>Start a conversation</div>"
        "<div style='color:%3;font-size:11px;'>"
        "Type a message, @ to mention files, / for commands</div>")
        .arg(thm2.hex("surface0"), thm2.hex("text_faint"), thm2.hex("surface0")));
    welcomeLabel->setTextFormat(Qt::RichText);
    welcomeLayout->addWidget(welcomeLabel);

    auto *welcomeChips = new SuggestionChips(welcome);
    welcomeChips->setSuggestions({
        "Explain this codebase",
        "Find bugs in recent changes",
        "Write tests for...",
        "Refactor..."
    });
    connect(welcomeChips, &SuggestionChips::suggestionClicked, this, [this](const QString &text) {
        m_inputBar->setText(text);
        m_inputBar->focusInput();
    });
    welcomeLayout->addWidget(welcomeChips, 0, Qt::AlignCenter);

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

void ChatPanel::restoreSession(const QString &sessionId)
{
    if (!m_database) return;

    ChatTab tab;
    tab.sessionId = sessionId;
    tab.container = createChatContent();
    tab.scrollArea = tab.container->findChild<QScrollArea *>();
    tab.messagesLayout = tab.scrollArea->widget()->findChild<QVBoxLayout *>("messagesLayout");

    tab.process = new ClaudeProcess(this);
    tab.process->setWorkingDirectory(m_workingDir);
    tab.process->setSessionId(sessionId);
    tab.sessionConfirmed = true;

    auto messages = m_database->loadMessages(sessionId);

    int maxTurn = 0;
    ToolCallGroupWidget *pendingGroup = nullptr;
    bool firstAssistantInTurn = true;
    int currentTurn = -1;

    auto flushGroup = [&] {
        if (pendingGroup) {
            pendingGroup->finalize();
            pendingGroup = nullptr;
        }
    };

    auto insertPos = [&] {
        return tab.messagesLayout->count() - 1;
    };

    for (const auto &msg : messages) {
        int turnId = msg.turnId;
        if (turnId > maxTurn) maxTurn = turnId;

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
            bool hasCheckpoint =
                !m_database->checkpointUuid(sessionId, turnId).isEmpty();
            w->showRevertButton(hasCheckpoint);
            tab.messagesLayout->insertWidget(insertPos(), w);
            connect(w, &ChatMessageWidget::revertRequested, this, &ChatPanel::onRevertRequested);

        } else if (msg.role == "thinking" && !msg.content.trimmed().isEmpty()) {
            flushGroup();
            auto *tb = new ThinkingBlockWidget;
            tb->appendContent(msg.content);
            tb->finalize();
            tab.messagesLayout->insertWidget(insertPos(), tb);

        } else if (msg.role == "assistant" && !msg.content.trimmed().isEmpty()) {
            flushGroup();
            auto *w = new ChatMessageWidget(ChatMessageWidget::Assistant, msg.content);
            w->setTurnId(turnId);
            if (msg.timestamp > 0)
                w->setTimestamp(QDateTime::fromSecsSinceEpoch(msg.timestamp));
            if (!firstAssistantInTurn)
                w->setHeaderVisible(false);
            firstAssistantInTurn = false;
            tab.messagesLayout->insertWidget(insertPos(), w);
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
                tab.messagesLayout->insertWidget(insertPos(), editGroup);
            } else {
                if (!pendingGroup) {
                    pendingGroup = new ToolCallGroupWidget;
                    connect(pendingGroup, &ToolCallGroupWidget::fileClicked, this, &ChatPanel::onToolFileClicked);
                    tab.messagesLayout->insertWidget(insertPos(), pendingGroup);
                }
                pendingGroup->addToolCall(info);
            }
        }
    }
    flushGroup();
    tab.turnId = maxTurn;

    auto *scrollContent = tab.scrollArea->widget();
    auto *indicator = new ThinkingIndicator(scrollContent);
    tab.messagesLayout->insertWidget(tab.messagesLayout->count() - 1, indicator);
    tab.thinkingIndicator = indicator;

    SessionInfo info = m_sessionMgr ? m_sessionMgr->sessionInfo(sessionId) : SessionInfo();
    QString title;
    if (!info.title.isEmpty()) {
        title = info.title;
    } else {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(info.createdAt);
        title = dt.isValid() ? QStringLiteral("Chat %1").arg(dt.toString("MMM d")) : "Chat";
    }
    int idx = m_tabWidget->addTab(tab.container, title);
    tab.tabIndex = idx;
    m_tabs[idx] = tab;

    wireProcessSignals(m_tabs[idx]);

    // Scroll monitoring: detect which turn is visible
    auto *scrollBar = m_tabs[idx].scrollArea->verticalScrollBar();
    connect(scrollBar, &QScrollBar::valueChanged, this, [this, idx]() {
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

    // Scroll to the end so user sees most recent messages
    scrollTabToBottom(m_tabs[idx]);

    emit activeSessionChanged(sessionId);
    emit sessionListChanged();

    // Populate effects panel with historical file changes and timestamps
    auto historicalChanges = extractFileChangesFromHistory(sessionId);
    if (!historicalChanges.isEmpty())
        emit historicalEffectsReady(sessionId, historicalChanges);

    auto timestamps = turnTimestampsForSession(sessionId);
    if (!timestamps.isEmpty())
        emit turnTimestampsReady(sessionId, timestamps);
}

void ChatPanel::sendMessage(const QString &text)
{
    onSendRequested(text);
}

void ChatPanel::onSendRequested(const QString &text)
{
    if (m_tabs.isEmpty())
        newChat();

    auto &tab = currentTab();

    // Hide previous suggestion chips
    if (tab.suggestionChips) {
        tab.suggestionChips->clear();
        tab.suggestionChips = nullptr;
    }

    tab.turnId++;
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
            "- `/mode <agent|ask|plan>` - Switch mode\n\n"
            "**Shortcuts:**\n"
            "- `@` - Mention files to attach as context\n"
            "- Paste images with Ctrl/Cmd+V\n"
            "- Cmd+K in editor for inline edits");
        addMessageToTab(tab, helpMsg);
    } else if (command == "/mode" && !args.isEmpty()) {
        m_modeSelector->setMode(args.toLower());
    } else if (command == "/model" && !args.isEmpty()) {
        // Model switching handled by ModelSelector
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
        s.updatedAt = QDateTime::currentSecsSinceEpoch();
        result.append(s);
        openIds.insert(it->sessionId);
    }

    // Second: old sessions from database (not currently open)
    if (m_database) {
        auto sessions = m_database->loadSessions();
        for (const auto &session : sessions) {
            if (session.workspace != m_workingDir) continue;
            if (openIds.contains(session.sessionId)) continue;
            AgentSummary s;
            s.sessionId = session.sessionId;
            s.title = session.title.isEmpty()
                          ? session.sessionId.left(8) + "..."
                          : session.title;
            s.updatedAt = session.updatedAt;
            s.turnCount = m_database->turnCountForSession(session.sessionId);
            result.append(s);
        }
    }

    return result;
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
    // Key by (turnId, filePath) to preserve per-turn grouping
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
    tab.currentAssistantMsg->appendContent(tab.pendingText);
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

    if (tab.tabIndex == m_tabWidget->currentIndex())
        refreshInputBarForCurrentTab();

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
        m_tabWidget->setTabIcon(tabIndex, dotIcon(QColor(thm.hex("mauve"))));
    } else if (tab.unread && tabIndex != m_tabWidget->currentIndex()) {
        m_tabWidget->setTabIcon(tabIndex, dotIcon(QColor(thm.hex("blue"))));
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
             thm.hex("blue"), fi.fileName().toHtmlEscaped());

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
            .arg(thm.hex("text_primary"), thm.hex("blue")));

        auto actionBtnStyle = QStringLiteral(
            "QPushButton { border: 1px solid %1; font-size: 11px;"
            " border-radius: 4px; background: %2; padding: 2px 6px; color: %5; }"
            "QPushButton:hover { background: %3; border-color: %4; }");

        auto *exportBtn = new QPushButton("Export", row);
        exportBtn->setCursor(Qt::PointingHandCursor);
        exportBtn->setStyleSheet(actionBtnStyle
            .arg(thm.hex("border_standard"), thm.hex("bg_base"),
                 thm.hex("bg_surface"), thm.hex("blue"),
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
