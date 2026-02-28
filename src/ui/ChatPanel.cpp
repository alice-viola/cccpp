#include "ui/ChatPanel.h"
#include "ui/InputBar.h"
#include "ui/ModeSelector.h"
#include "ui/ModelSelector.h"
#include "ui/ChatMessageWidget.h"
#include "ui/ToolCallGroupWidget.h"
#include "ui/ThinkingIndicator.h"
#include "ui/QuestionWidget.h"
#include "ui/SuggestionChips.h"
#include "ui/ThemeManager.h"
#include "ui/CodeViewer.h"
#include "core/ClaudeProcess.h"
#include "core/StreamParser.h"
#include "core/SessionManager.h"
#include "core/SnapshotManager.h"
#include "core/DiffEngine.h"
#include "core/Database.h"
#include "util/JsonUtils.h"
#include <QScrollBar>
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
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

    cornerLayout->addWidget(m_newChatBtn);
    cornerLayout->addWidget(m_historyBtn);
    m_tabWidget->setCornerWidget(cornerWidget, Qt::TopRightCorner);

    mainLayout->addWidget(m_tabWidget, 1);

    // Mode + Model selectors
    auto *selectorRow = new QWidget(this);
    auto *selectorLayout = new QHBoxLayout(selectorRow);
    selectorLayout->setContentsMargins(0, 0, 0, 0);
    selectorLayout->setSpacing(0);

    m_modeSelector = new ModeSelector(this);
    m_modelSelector = new ModelSelector(this);
    selectorLayout->addWidget(m_modeSelector);
    selectorLayout->addWidget(m_modelSelector);

    mainLayout->addWidget(selectorRow);

    m_inputBar = new InputBar(this);
    mainLayout->addWidget(m_inputBar);

    connect(m_inputBar, &InputBar::sendRequested, this, &ChatPanel::onSendRequested);
    connect(m_inputBar, &InputBar::slashCommand, this, &ChatPanel::onSlashCommand);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int idx) {
        refreshInputBarForCurrentTab();
        if (m_tabs.contains(idx))
            emit activeSessionChanged(m_tabs[idx].sessionId);
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
}

void ChatPanel::setSessionManager(SessionManager *mgr) { m_sessionMgr = mgr; }
void ChatPanel::setSnapshotManager(SnapshotManager *snap) { m_snapshotMgr = snap; }
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
    int tabIdx = tab.tabIndex;

    connect(proc->streamParser(), &StreamParser::textDelta, this,
            [this, tabIdx](const QString &text) {
        if (!m_tabs.contains(tabIdx)) return;
        auto &t = m_tabs[tabIdx];
        if (t.currentAssistantMsg)
            t.currentAssistantMsg->appendContent(text);
        scrollTabToBottom(t);
    });

    connect(proc->streamParser(), &StreamParser::toolUseStarted, this,
            [this, tabIdx](const QString &name, const nlohmann::json &input) {
        if (!m_tabs.contains(tabIdx)) return;
        auto &t = m_tabs[tabIdx];

        bool hasOldStr = input.contains("old_string");
        bool hasPath = input.contains("path") || input.contains("file_path");
        qDebug() << "[cccpp] Tool:" << name
                 << "hasPath:" << hasPath
                 << "hasOldString:" << hasOldStr
                 << "inputKeys:" << QString::fromStdString(input.dump()).left(100);

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

        if ((name == "Edit" || name == "StrReplace") && input.contains("old_string")) {
            info.isEdit = true;
            info.oldString = JsonUtils::getString(input, "old_string");
            info.newString = JsonUtils::getString(input, "new_string");

            if (m_snapshotMgr)
                m_snapshotMgr->recordEditOldString(info.filePath, info.oldString);
            if (m_diffEngine)
                m_diffEngine->recordEditToolChange(info.filePath, info.oldString, info.newString);
            t.pendingEditFile = info.filePath;
            emit fileChanged(info.filePath);

            if (t.currentAssistantMsg && !info.filePath.isEmpty()) {
                QString diffHtml = buildInlineDiffHtml(info.filePath, info.oldString, info.newString);
                QString summary = QStringLiteral("\n[Edit: %1]\n").arg(QFileInfo(info.filePath).fileName());
                t.currentAssistantMsg->appendRawHtml(diffHtml, summary);
            }
            emit editApplied(info.filePath, info.oldString, info.newString, 0);
        } else if (name == "Write" && !info.filePath.isEmpty()) {
            info.isEdit = true;
            info.newString = JsonUtils::getString(input, "content",
                             JsonUtils::getString(input, "contents"));
            if (m_diffEngine)
                m_diffEngine->recordWriteToolChange(info.filePath, info.newString);

            // Record old file content for snapshot (empty string if file is new)
            if (m_snapshotMgr) {
                QString oldContent;
                QFile existingFile(info.filePath);
                if (existingFile.open(QIODevice::ReadOnly | QIODevice::Text))
                    oldContent = QString::fromUtf8(existingFile.readAll());
                m_snapshotMgr->recordEditOldString(info.filePath, oldContent);
            }

            t.pendingEditFile = info.filePath;
            emit fileChanged(info.filePath);

            if (t.currentAssistantMsg) {
                int lineCount = info.newString.count('\n') + 1;
                QString diffHtml = buildInlineDiffHtml(
                    info.filePath, "",
                    QStringLiteral("(%1 lines written)").arg(lineCount));
                QString summary = QStringLiteral("\n[Write: %1 (%2 lines)]\n")
                    .arg(QFileInfo(info.filePath).fileName()).arg(lineCount);
                t.currentAssistantMsg->appendRawHtml(diffHtml, summary);
            }

            if (info.filePath.contains("/.claude/plans/") && info.filePath.endsWith(".md"))
                emit planFileDetected(info.filePath);
        }

        if (name == "AskUserQuestion") {
            auto *questionWidget = new QuestionWidget(input);
            if (t.messagesLayout) {
                int count = t.messagesLayout->count();
                t.messagesLayout->insertWidget(count - 1, questionWidget);
            }
            connect(questionWidget, &QuestionWidget::answered, this,
                    [this, tabIdx](const QString &response) {
                if (!m_tabs.contains(tabIdx)) return;
                auto &tab = m_tabs[tabIdx];
                tab.process->setMode(m_modeSelector->currentMode());
                tab.currentAssistantMsg = new ChatMessageWidget(ChatMessageWidget::Assistant, "");
                tab.currentAssistantMsg->setTurnId(tab.turnId);
                if (tab.messagesLayout) {
                    int count = tab.messagesLayout->count();
                    tab.messagesLayout->insertWidget(count - 1, tab.currentAssistantMsg);
                }
                setTabProcessingState(tabIdx, true);
                tab.process->sendMessage(response);
            });
            scrollTabToBottom(t);
            goto persistTool;
        }

        if (!t.currentToolGroup) {
            t.currentToolGroup = new ToolCallGroupWidget;
            if (t.messagesLayout) {
                int count = t.messagesLayout->count();
                t.messagesLayout->insertWidget(count - 1, t.currentToolGroup);
            }
        }
        t.currentToolGroup->addToolCall(info);
        scrollTabToBottom(t);

        persistTool:
        if (m_database) {
            MessageRecord rec;
            rec.sessionId = t.sessionId;
            rec.role = "tool";
            rec.content = info.summary;
            rec.toolName = name;
            rec.toolInput = QString::fromStdString(input.dump());
            rec.turnId = t.turnId;
            rec.timestamp = QDateTime::currentSecsSinceEpoch();
            m_database->saveMessage(rec);
        }
    });

    connect(proc->streamParser(), &StreamParser::toolResultReceived, this,
            [this, tabIdx](const QString &) {
        if (!m_tabs.contains(tabIdx)) return;
        auto &t = m_tabs[tabIdx];
        if (!t.pendingEditFile.isEmpty()) {
            emit fileChanged(t.pendingEditFile);
            t.pendingEditFile.clear();
        }
    });

    connect(proc->streamParser(), &StreamParser::resultReady, this,
            [this, tabIdx](const QString &sessionId, const nlohmann::json &) {
        if (!m_tabs.contains(tabIdx)) return;
        auto &t = m_tabs[tabIdx];
        if (!sessionId.isEmpty() && t.sessionId != sessionId) {
            QString oldId = t.sessionId;
            t.sessionId = sessionId;
            t.process->setSessionId(sessionId);
            if (m_sessionMgr)
                m_sessionMgr->updateSessionId(oldId, sessionId);
            if (m_database)
                m_database->updateMessageSessionId(oldId, sessionId);
        }
    });

    connect(proc, &ClaudeProcess::finished, this, [this, tabIdx](int exitCode) {
        if (!m_tabs.contains(tabIdx)) return;
        auto &t = m_tabs[tabIdx];

        if (m_snapshotMgr)
            m_snapshotMgr->commitTurn();

        if (t.currentToolGroup) {
            t.currentToolGroup->finalize();
            t.currentToolGroup = nullptr;
        }

        if (t.currentAssistantMsg && t.currentAssistantMsg->rawContent().isEmpty()) {
            t.currentAssistantMsg->appendContent(
                QStringLiteral("*(Process exited with code %1)*").arg(exitCode));
        }

        if (m_database && t.currentAssistantMsg) {
            QString content = t.currentAssistantMsg->rawContent().trimmed();
            bool isNoise = content.isEmpty()
                || content.startsWith("*(Process exited")
                || content == "*(Process exited with code 0)*";
            if (!isNoise) {
                MessageRecord rec;
                rec.sessionId = t.sessionId;
                rec.role = "assistant";
                rec.content = content;
                rec.turnId = t.turnId;
                rec.timestamp = QDateTime::currentSecsSinceEpoch();
                m_database->saveMessage(rec);
            }
        }

        if (t.currentAssistantMsg) {
            t.currentAssistantMsg->showRevertButton(true);

            // Generate follow-up suggestions
            showSuggestionChips(t, t.currentAssistantMsg->rawContent());

            t.currentAssistantMsg = nullptr;
        }
        setTabProcessingState(tabIdx, false);
        scrollTabToBottom(t);
    });

    connect(proc, &ClaudeProcess::errorOccurred, this, [this, tabIdx](const QString &err) {
        qWarning() << "ClaudeProcess error (tab" << tabIdx << "):" << err;
        if (!m_tabs.contains(tabIdx)) return;
        auto &t = m_tabs[tabIdx];
        if (t.currentAssistantMsg)
            t.currentAssistantMsg->appendContent(
                QStringLiteral("\n\n**Error:** %1").arg(err));
        setTabProcessingState(tabIdx, false);
    });

    connect(proc->streamParser(), &StreamParser::errorOccurred, this,
            [this, tabIdx](const QString &err) {
        if (!m_tabs.contains(tabIdx)) return;
        auto &t = m_tabs[tabIdx];
        if (t.currentAssistantMsg)
            t.currentAssistantMsg->appendContent(
                QStringLiteral("\n\n**Stream error:** %1").arg(err));
    });
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
    auto *welcome = new QLabel(scrollContent);
    welcome->setObjectName("chatWelcome");
    welcome->setAlignment(Qt::AlignCenter);
    auto &thm2 = ThemeManager::instance();
    welcome->setText(
        QStringLiteral(
        "<div style='color:%1;font-size:32px;margin-bottom:16px;'>&#x2726;</div>"
        "<div style='color:%2;font-size:14px;font-weight:500;"
        "margin-bottom:6px;'>Start a conversation</div>"
        "<div style='color:%3;font-size:11px;'>"
        "Type a message, @ to mention files, / for commands</div>")
        .arg(thm2.hex("surface0"), thm2.hex("text_faint"), thm2.hex("surface0")));
    welcome->setTextFormat(Qt::RichText);
    welcome->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tab.messagesLayout->insertWidget(0, welcome);
    tab.welcomeWidget = welcome;

    auto *indicator = new ThinkingIndicator(scrollContent);
    tab.messagesLayout->insertWidget(tab.messagesLayout->count() - 1, indicator);
    tab.thinkingIndicator = indicator;

    int idx = m_tabWidget->addTab(tab.container, "New Chat");
    tab.tabIndex = idx;
    m_tabs[idx] = tab;

    wireProcessSignals(m_tabs[idx]);

    m_tabWidget->setCurrentIndex(idx);
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

    auto messages = m_database->loadMessages(sessionId);

    struct TurnData {
        QString userContent;
        QString assistantContent;
        QList<MessageRecord> tools;
    };
    QMap<int, TurnData> turns;
    int maxTurn = 0;

    for (const auto &msg : messages) {
        int t = msg.turnId;
        if (t > maxTurn) maxTurn = t;
        if (msg.role == "user" && !msg.content.trimmed().isEmpty())
            turns[t].userContent = msg.content;
        else if (msg.role == "assistant" && !msg.content.trimmed().isEmpty())
            turns[t].assistantContent = msg.content;
        else if (msg.role == "tool")
            turns[t].tools.append(msg);
    }

    for (auto it = turns.constBegin(); it != turns.constEnd(); ++it) {
        const TurnData &td = it.value();
        int turnId = it.key();

        if (!td.userContent.isEmpty()) {
            auto *w = new ChatMessageWidget(ChatMessageWidget::User, td.userContent);
            w->setTurnId(turnId);
            tab.messagesLayout->insertWidget(tab.messagesLayout->count() - 1, w);
        }
        if (!td.assistantContent.isEmpty()) {
            auto *w = new ChatMessageWidget(ChatMessageWidget::Assistant, td.assistantContent);
            w->setTurnId(turnId);
            w->showRevertButton(true);
            tab.messagesLayout->insertWidget(tab.messagesLayout->count() - 1, w);
            connect(w, &ChatMessageWidget::revertRequested, this, &ChatPanel::onRevertRequested);
            connect(w, &ChatMessageWidget::fileNavigationRequested,
                    this, [this](const QString &fp, int line) { emit navigateToFile(fp, line); });
        }
        if (!td.tools.isEmpty()) {
            auto *group = new ToolCallGroupWidget;
            for (const auto &toolMsg : td.tools) {
                ToolCallInfo info;
                info.toolName = toolMsg.toolName;
                info.summary = toolMsg.content;
                group->addToolCall(info);
            }
            group->finalize();
            tab.messagesLayout->insertWidget(tab.messagesLayout->count() - 1, group);
        }
    }
    tab.turnId = maxTurn;

    auto *scrollContent = tab.scrollArea->widget();
    auto *indicator = new ThinkingIndicator(scrollContent);
    tab.messagesLayout->insertWidget(tab.messagesLayout->count() - 1, indicator);
    tab.thinkingIndicator = indicator;

    SessionInfo info = m_sessionMgr ? m_sessionMgr->sessionInfo(sessionId) : SessionInfo();
    QString title = info.title.isEmpty() ? sessionId.left(8) : info.title;
    int idx = m_tabWidget->addTab(tab.container, title);
    tab.tabIndex = idx;
    m_tabs[idx] = tab;

    wireProcessSignals(m_tabs[idx]);

    m_tabWidget->setCurrentIndex(idx);

    QString lastPlanFile;
    for (const auto &msg : messages) {
        if (msg.role == "tool" && msg.content.startsWith("Write: ")) {
            QString path = msg.content.mid(7).trimmed();
            if (path.contains("/.claude/plans/") && path.endsWith(".md"))
                lastPlanFile = path;
        }
    }
    if (!lastPlanFile.isEmpty() && QFile::exists(lastPlanFile))
        emit planFileDetected(lastPlanFile);

    emit activeSessionChanged(sessionId);
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

    auto *userMsg = new ChatMessageWidget(ChatMessageWidget::User, text);
    addMessageToTab(tab, userMsg);

    tab.turnId++;
    emit aboutToSendMessage();
    if (m_snapshotMgr) {
        m_snapshotMgr->setSessionId(tab.sessionId);
        m_snapshotMgr->beginTurn(tab.turnId);
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

    tab.currentAssistantMsg = new ChatMessageWidget(ChatMessageWidget::Assistant, "");
    tab.currentAssistantMsg->setTurnId(tab.turnId);
    addMessageToTab(tab, tab.currentAssistantMsg);

    // Build enriched message with context
    QString enrichedMessage = buildContextPreamble(text);

    tab.process->setMode(m_modeSelector->currentMode());
    tab.process->setModel(m_modelSelector->currentModelId());
    if (!tab.sessionId.startsWith("pending-"))
        tab.process->setSessionId(tab.sessionId);

    setTabProcessingState(tab.tabIndex, true);
    tab.process->sendMessage(enrichedMessage);

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

void ChatPanel::onRevertRequested(int turnId)
{
    if (m_snapshotMgr)
        m_snapshotMgr->revertTurn(turnId);
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

    // Image contexts
    auto images = m_inputBar->attachedImages();
    if (!images.isEmpty()) {
        contextParts << QStringLiteral("[%1 image(s) attached]").arg(images.size());
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

void ChatPanel::showSuggestionChips(ChatTab &tab, const QString &responseText)
{
    QStringList suggestions;

    // Heuristic: if response mentions files that were edited, suggest reviewing them
    if (responseText.contains("Edit:") || responseText.contains("Write:") ||
        responseText.contains("created") || responseText.contains("modified")) {
        suggestions << "Run tests" << "Show diff";
    }

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

    if (tab.messagesLayout) {
        int count = tab.messagesLayout->count();
        tab.messagesLayout->insertWidget(count - 1, chips);
    }
    tab.suggestionChips = chips;
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
    messagesLayout->setContentsMargins(16, 12, 16, 12);
    messagesLayout->setSpacing(12);
    messagesLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    containerLayout->addWidget(scrollArea);

    return container;
}

void ChatPanel::addMessageToTab(ChatTab &tab, ChatMessageWidget *msg)
{
    if (!tab.messagesLayout) return;

    if (tab.welcomeWidget && tab.welcomeWidget->isVisible())
        tab.welcomeWidget->hide();

    int insertPos = tab.messagesLayout->count() - 1;
    if (tab.thinkingIndicator) {
        int indicatorIdx = tab.messagesLayout->indexOf(tab.thinkingIndicator);
        if (indicatorIdx >= 0) insertPos = indicatorIdx;
    }
    tab.messagesLayout->insertWidget(insertPos, msg);

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

void ChatPanel::scrollTabToBottom(ChatTab &tab)
{
    if (!tab.scrollArea) return;
    QTimer::singleShot(10, this, [sa = tab.scrollArea] {
        auto *sb = sa->verticalScrollBar();
        sb->setValue(sb->maximum());
    });
}

void ChatPanel::setTabProcessingState(int tabIdx, bool processing)
{
    if (!m_tabs.contains(tabIdx)) return;
    auto &tab = m_tabs[tabIdx];
    tab.processing = processing;

    if (tab.thinkingIndicator) {
        if (processing)
            tab.thinkingIndicator->startAnimation();
        else
            tab.thinkingIndicator->stopAnimation();
    }

    if (tabIdx == m_tabWidget->currentIndex())
        refreshInputBarForCurrentTab();

    bool anyProcessing = false;
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (it->processing) { anyProcessing = true; break; }
    }
    emit processingChanged(anyProcessing);
}

void ChatPanel::refreshInputBarForCurrentTab()
{
    int idx = m_tabWidget->currentIndex();
    bool busy = m_tabs.contains(idx) && m_tabs[idx].processing;
    m_inputBar->setEnabled(!busy);
    m_inputBar->setPlaceholder(busy ? "Claude is thinking..."
                                    : "Ask Claude anything... (@ to mention files, / for commands)");
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
        "\n\n<table cellspacing='0' cellpadding='0' style='width:100%%;margin:6px 0;'><tr><td>"
        "<div style='background:%4;border:1px solid %5;border-radius:6px;overflow:hidden;'>"
        "<div style='background:%6;padding:4px 8px;border-bottom:1px solid %5;'>"
        "<a href='cccpp://open?file=%1&line=%3' style='color:%7;text-decoration:none;font-size:11px;"
        "font-family:\"SF Mono\",\"JetBrains Mono\",\"Fira Code\",\"Menlo\",\"Consolas\",monospace;'>\xf0\x9f\x93\x84 %2</a></div>")
        .arg(filePath.toHtmlEscaped(), fi.fileName().toHtmlEscaped(), QString::number(editLine),
             thm.hex("bg_base"), thm.hex("border_standard"), thm.hex("bg_surface"), thm.hex("blue"));

    html += "<div style='padding:2px 0;font-family:\"SF Mono\",\"JetBrains Mono\",\"Fira Code\",\"Menlo\",\"Consolas\",monospace;font-size:12px;line-height:1.4;'>";

    if (!oldStr.isEmpty()) {
        QStringList oldLines = oldStr.split('\n');
        int maxLines = qMin(oldLines.size(), 10);
        for (int i = 0; i < maxLines; ++i)
            html += QStringLiteral(
                "<div style='background:%2;color:%3;padding:0 8px;white-space:pre;'>-%1</div>")
                .arg(oldLines[i].toHtmlEscaped(), thm.hex("diff_del_bg"), thm.hex("red"));
        if (oldLines.size() > maxLines)
            html += QStringLiteral(
                "<div style='color:%2;padding:0 8px;font-size:11px;'>... %1 more</div>")
                .arg(oldLines.size() - maxLines).arg(thm.hex("text_faint"));
    }

    if (!newStr.isEmpty()) {
        QStringList newLines = newStr.split('\n');
        int maxLines = qMin(newLines.size(), 10);
        for (int i = 0; i < maxLines; ++i)
            html += QStringLiteral(
                "<div style='background:%2;color:%3;padding:0 8px;white-space:pre;'>+%1</div>")
                .arg(newLines[i].toHtmlEscaped(), thm.hex("diff_add_bg"), thm.hex("green"));
        if (newLines.size() > maxLines)
            html += QStringLiteral(
                "<div style='color:%2;padding:0 8px;font-size:11px;'>... %1 more</div>")
                .arg(newLines.size() - maxLines).arg(thm.hex("text_faint"));
    }

    html += "</div></div></td></tr></table>";
    return html;
}

void ChatPanel::showHistoryMenu()
{
    if (!m_database) return;

    auto sessions = m_database->loadSessions();
    if (sessions.isEmpty()) return;

    QMenu menu(this);

    QSet<QString> openIds;
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it)
        openIds.insert(it->sessionId);

    int count = 0;
    for (const auto &session : sessions) {
        if (session.workspace != m_workingDir) continue;
        if (session.sessionId.startsWith("pending-")) continue;
        if (openIds.contains(session.sessionId)) continue;

        QString label = session.title;
        if (label.isEmpty())
            label = session.sessionId.left(8) + "...";

        QDateTime dt = QDateTime::fromSecsSinceEpoch(session.updatedAt);
        label += "  " + dt.toString("MMM d, hh:mm");

        auto *action = menu.addAction(label);
        QString sid = session.sessionId;
        connect(action, &QAction::triggered, this, [this, sid] {
            restoreSession(sid);
        });

        if (++count >= 20) break;
    }

    if (count == 0) {
        menu.addAction("No previous chats")->setEnabled(false);
    }

    menu.exec(m_historyBtn->mapToGlobal(QPoint(0, m_historyBtn->height())));
}
