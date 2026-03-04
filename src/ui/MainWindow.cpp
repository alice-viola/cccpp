#include "ui/MainWindow.h"
#include "ui/WorkspaceTree.h"
#include "ui/CodeViewer.h"
#include "ui/ChatPanel.h"
#include "ui/TerminalPanel.h"
#include "ui/GitPanel.h"
#include "ui/SearchPanel.h"
#include "ui/ModelSelector.h"
#include "ui/ToastManager.h"
#include "ui/ThemeManager.h"
#include "ui/SettingsDialog.h"
#include "ui/InputBar.h"
#include "ui/AgentFleetPanel.h"
#include "ui/EffectsPanel.h"
#include "core/SessionManager.h"
#include "core/DiffEngine.h"
#include "core/Database.h"
#include "core/GitManager.h"
#include "core/TelegramApi.h"
#include "core/TelegramBridge.h"
#include "core/TelegramDaemon.h"
#include "core/DaemonClient.h"
#include "core/ClaudeProcess.h"
#include "core/StreamParser.h"
#include "util/Config.h"
#include "util/MacUtils.h"
#include "util/JsonUtils.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QMap>
#include <QApplication>
#include <QClipboard>
#include <QTextEdit>
#include <QLabel>
#include <QScreen>
#include <QStatusBar>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QDebug>
#include <QMessageBox>
#include <QShortcut>

static constexpr int    kTreePanelWidth     = 250;
static constexpr double kEditorFraction     = 0.40;
static constexpr double kChatFraction       = 0.35;
static constexpr double kEditorFractionGit  = 0.50;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("CCCPP - Claude Code C++ UI");

    m_sessionMgr = new SessionManager(this);
    m_diffEngine = new DiffEngine(this);
    m_database = new Database(this);
    m_database->open();
    m_database->deleteStalePendingSessions();
    m_gitManager = new GitManager(this);

    ThemeManager::instance().initialize();

    setupUI();
    setupMenuBar();
    setupStatusBar();
    loadStylesheet();
    connectGitSignals();
    ToastManager::instance().setParentWidget(this);

    showMaximized();

    QTimer::singleShot(0, this, [this] {
        // Manager View initial sizes: [Fleet=300, LeftTabs=0, Center=0, Chat=flex, Effects=300]
        int total = m_splitter->width();
        int fleetW = 300;
        int effectsW = 300;
        int chatW = total - fleetW - effectsW;
        m_splitter->setSizes({fleetW, 0, 0, chatW, effectsW});
        updateToggleButtons();
    });

    QString lastWorkspace = Config::instance().lastWorkspace();
    if (!lastWorkspace.isEmpty())
        openWorkspace(lastWorkspace);

    connect(m_sessionMgr, &SessionManager::sessionCreated, this, [this](const QString &id) {
        m_database->saveSession(m_sessionMgr->sessionInfo(id));
    });
    connect(m_sessionMgr, &SessionManager::sessionUpdated, this, [this](const QString &id) {
        m_database->saveSession(m_sessionMgr->sessionInfo(id));
    });

    setupTelegram();
}

MainWindow::~MainWindow()
{
    if (m_daemonClient)
        m_daemonClient->unregisterWorkspace();
    if (m_telegramApi)
        m_telegramApi->stopPolling();
}

void MainWindow::setupUI()
{
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(2);

    m_workspaceTree = new WorkspaceTree(this);
    m_codeViewer = new CodeViewer(this);
    m_codeViewer->setGitManager(m_gitManager);
    m_terminalPanel = new TerminalPanel(this);
    m_chatPanel = new ChatPanel(this);
    m_gitPanel = new GitPanel(this);
    m_gitPanel->setGitManager(m_gitManager);
    m_searchPanel = new SearchPanel(this);

    m_chatPanel->setSessionManager(m_sessionMgr);
    m_chatPanel->setDiffEngine(m_diffEngine);
    m_chatPanel->setDatabase(m_database);
    m_chatPanel->setCodeViewer(m_codeViewer);

    // Mission Control: Agent Fleet (left panel)
    m_agentFleet = new AgentFleetPanel(this);

    // Left tabs (workspace tree, git, search) — used in Editor View
    m_leftTabs = new QTabWidget(this);
    m_leftTabs->setDocumentMode(true);
    m_leftTabs->setTabPosition(QTabWidget::South);
    m_leftTabs->addTab(m_workspaceTree, "Files");
    m_leftTabs->addTab(m_gitPanel, "Git");
    m_leftTabs->addTab(m_searchPanel, "Find");

    // Center splitter (code editor + terminal) — used in Editor View
    m_centerSplitter = new QSplitter(Qt::Vertical, this);
    m_centerSplitter->setHandleWidth(2);
    m_centerSplitter->addWidget(m_codeViewer);
    m_centerSplitter->addWidget(m_terminalPanel);
    m_centerSplitter->setStretchFactor(0, 3);
    m_centerSplitter->setStretchFactor(1, 1);
    m_terminalPanel->hide();

    // Mission Control: Effects Panel (right panel)
    m_effectsPanel = new EffectsPanel(this);

    // ── 5-widget splitter layout ──
    // [0] AgentFleet  [1] LeftTabs  [2] CenterSplitter  [3] ChatPanel  [4] EffectsPanel
    //
    // Manager View: [0]=220, [1]=hidden, [2]=hidden, [3]=flex, [4]=300
    // Editor View:  [0]=40, [1]=210, [2]=flex, [3]=320, [4]=hidden
    m_splitter->addWidget(m_agentFleet);
    m_splitter->addWidget(m_leftTabs);
    m_splitter->addWidget(m_centerSplitter);
    m_splitter->addWidget(m_chatPanel);
    m_splitter->addWidget(m_effectsPanel);

    for (int i = 0; i < 5; ++i)
        m_splitter->setCollapsible(i, false);

    m_agentFleet->setMinimumWidth(40);
    m_chatPanel->setMinimumWidth(200);
    m_effectsPanel->setMinimumWidth(100);

    // Start in Manager View: hide left tabs and code editor
    m_leftTabs->hide();
    m_codeViewer->hide();
    m_centerSplitter->hide();

    // Hide ChatPanel's tab bar — Fleet drives selection now
    m_chatPanel->hideTabBar();

    m_splitter->setStretchFactor(0, 0);  // AgentFleet: fixed
    m_splitter->setStretchFactor(1, 0);  // LeftTabs: fixed (hidden)
    m_splitter->setStretchFactor(2, 0);  // CenterSplitter: fixed (hidden)
    m_splitter->setStretchFactor(3, 1);  // ChatPanel: flex
    m_splitter->setStretchFactor(4, 0);  // EffectsPanel: fixed

    setCentralWidget(m_splitter);

    connect(m_workspaceTree, &WorkspaceTree::fileSelected,
            this, &MainWindow::onFileSelected);
    connect(m_workspaceTree, &WorkspaceTree::fileDeleted, this, [this](const QString &path) {
        m_codeViewer->closeFile(path);
        m_gitManager->refreshStatus();
    });
    connect(m_workspaceTree, &WorkspaceTree::fileCreated, this, [this](const QString &) {
        m_gitManager->refreshStatus();
    });
    connect(m_workspaceTree, &WorkspaceTree::folderCreated, this, [this](const QString &) {
        m_gitManager->refreshStatus();
    });
    connect(m_workspaceTree, &WorkspaceTree::folderDeleted, this, [this](const QString &) {
        m_gitManager->refreshStatus();
    });

    connect(m_searchPanel, &SearchPanel::fileSelected, this,
            [this](const QString &filePath, int line) {
        onFileSelected(filePath);
        if (line > 0)
            m_codeViewer->scrollToLine(line);
    });

    connect(m_chatPanel, &ChatPanel::fileChanged,
            this, &MainWindow::onFileChanged);
    connect(m_diffEngine, &DiffEngine::fileChanged, this,
            [this](const QString &filePath, const FileDiff &diff) {
        FileChangeType type = FileChangeType::Modified;
        if (diff.isNewFile) type = FileChangeType::Created;
        if (diff.isDeleted) type = FileChangeType::Deleted;
        m_workspaceTree->markFileChanged(filePath, type);
        m_codeViewer->refreshFile(filePath);
        if (m_codeViewer->currentFile() == filePath)
            m_codeViewer->showDiff(diff);
    });
    connect(m_chatPanel, &ChatPanel::navigateToFile, this,
            [this](const QString &filePath, int line) {
        QString resolved = filePath;
        if (!resolved.startsWith('/') && !m_workspacePath.isEmpty())
            resolved = m_workspacePath + "/" + resolved;

        m_centerSplitter->show();
        m_codeViewer->show();
        m_codeViewer->loadFile(resolved);
        m_statusFile->setText(QFileInfo(resolved).fileName());

        FileDiff diff = m_diffEngine->diffForFile(resolved);
        if (!diff.hunks.isEmpty())
            m_codeViewer->showDiff(diff);
        if (line > 0)
            m_codeViewer->scrollToLine(line);

        if (m_viewMode == ViewMode::Manager) {
            // Show inline file preview with back button (same as Effects click)
            m_chatPanel->hide();
            int total = m_splitter->width();
            int fleetW = m_agentFleet->isVisible() ? m_splitter->sizes()[0] : 0;
            int effectsW = m_effectsPanel->isVisible() ? m_splitter->sizes()[4] : 0;
            int centerW = total - fleetW - effectsW;
            m_splitter->setSizes({fleetW, 0, centerW, 0, effectsW});
            showFileBar(QFileInfo(resolved).fileName());
        }

        syncEditorContextToChat();
    });
    connect(m_chatPanel, &ChatPanel::planFileDetected, this,
            [this](const QString &filePath) {
        m_centerSplitter->show();
        m_codeViewer->show();
        m_codeViewer->openMarkdown(filePath);

        if (m_viewMode == ViewMode::Manager) {
            m_chatPanel->hide();
            int total = m_splitter->width();
            int fleetW = 300;
            int effectsW = 300;
            int centerW = total - fleetW - effectsW;
            m_splitter->setSizes({fleetW, 0, centerW, 0, effectsW});

            // Show breadcrumb for plan file
            showFileBar(QFileInfo(filePath).fileName());
        }
    });
    connect(m_chatPanel, &ChatPanel::aboutToSendMessage,
            this, &MainWindow::onBeforeTurnBegins);

    connect(m_codeViewer, &CodeViewer::fileSaved, this, [this](const QString &) {
        syncEditorContextToChat();
    });

    // Show Cursor-style inline diff when Claude edits a file
    connect(m_chatPanel, &ChatPanel::editApplied, this,
            [this](const QString &filePath, const QString &oldText, const QString &newText, int startLine) {
        if (!oldText.isEmpty() || !newText.isEmpty())
            m_codeViewer->showInlineDiff(filePath, oldText, newText, startLine);
    });

    // Apply code from chat to editor
    connect(m_chatPanel, &ChatPanel::applyCodeRequested, this,
            [this](const QString &code, const QString &language, const QString &) {
        Q_UNUSED(language);
        QString currentFile = m_codeViewer->currentFile();
        if (!currentFile.isEmpty())
            m_codeViewer->showInlineDiff(currentFile, "", code, 0);
    });

    // Cmd+K inline edit — dedicated process, never touches chat history
    connect(m_codeViewer, &CodeViewer::inlineEditSubmitted, this,
            [this](const QString &filePath, const QString &selectedCode,
                   const QString &instruction, int startLine, int endLine,
                   const QString &modelId) {
        // bar stays visible and transitions to "processing" state inside executeInlineEdit
        executeInlineEdit(filePath, selectedCode, instruction, startLine, endLine, modelId);
    });

    // Cmd+K Accept All — diff is already on disk, just clear state
    connect(m_codeViewer, &CodeViewer::inlineCmdKAccepted, this,
            [this](const QString &) {
        m_statusProcessing->setText("");
        m_inlineEditFile.clear();
        m_inlineEditOriginalContent.clear();
        m_gitManager->refreshStatus();
        ToastManager::instance().show("Edit accepted", ToastType::Success, 2000);
    });

    // Cmd+K Reject All — restore original content from snapshot
    connect(m_codeViewer, &CodeViewer::inlineCmdKRejected, this,
            [this](const QString &filePath) {
        m_statusProcessing->setText("");
        if (!m_inlineEditOriginalContent.isEmpty()) {
            QFile f(filePath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(m_inlineEditOriginalContent.toUtf8());
                f.close();
            }
            m_codeViewer->forceReloadFile(filePath);
        }
        m_inlineEditFile.clear();
        m_inlineEditOriginalContent.clear();
        m_gitManager->refreshStatus();
    });

    // Cmd+K Cancel — user cancelled during processing
    connect(m_codeViewer, &CodeViewer::inlineCmdKCancelled, this, [this] {
        if (m_inlineEditProcess) {
            m_inlineEditProcess->cancel();
            m_inlineEditProcess->deleteLater();
            m_inlineEditProcess = nullptr;
        }
        m_statusProcessing->setText("");
        m_inlineEditFile.clear();
        m_inlineEditOriginalContent.clear();
    });

    // Inline diff accept/reject (from chat edits, not Cmd+K)
    connect(m_codeViewer, &CodeViewer::inlineDiffAccepted, this,
            [this](const QString &) {
        // chat edits: no extra action needed (file already correct on disk)
    });
    connect(m_codeViewer, &CodeViewer::inlineDiffRejected, this,
            [this](const QString &filePath, const QString &, const QString &) {
        // Only handle per-hunk reject from chat edits (not Cmd+K, which uses inlineCmdKRejected)
        if (filePath != m_inlineEditFile) {
            m_chatPanel->rewindCurrentTurn();
            m_codeViewer->refreshFile(filePath);
        }
    });

    // Handle rewind completion from ChatPanel revert button
    connect(m_chatPanel, &ChatPanel::rewindCompleted, this, [this](bool success) {
        if (success) {
            ToastManager::instance().show("Files restored", ToastType::Success, 2500);
            m_gitManager->refreshStatus();
            for (const QString &fp : m_codeViewer->openFiles())
                m_codeViewer->forceReloadFile(fp);
        } else {
            ToastManager::instance().show("Rewind failed", ToastType::Error, 3000);
        }
    });

    // Git panel: open files on request
    connect(m_gitPanel, &GitPanel::requestOpenFile, this, [this](const QString &filePath) {
        QString fullPath = m_workspacePath + "/" + filePath;
        onFileSelected(fullPath);
    });

    // Git panel: file clicked -> show split diff
    connect(m_gitPanel, &GitPanel::fileClicked, this, [this](const QString &filePath, bool staged) {
        m_gitManager->requestFileDiff(filePath, staged);
    });

    // Cmd+K shortcut for inline edit
    auto *inlineEditShortcut = new QShortcut(QKeySequence("Ctrl+K"), this);
    connect(inlineEditShortcut, &QShortcut::activated, this, &MainWindow::onInlineEdit);

    // ── Mission Control: Agent Fleet wiring ──
    connect(m_agentFleet, &AgentFleetPanel::agentSelected, this, [this](const QString &sid) {
        dismissInlineFilePreview();
        m_chatPanel->selectSession(sid);
    });
    connect(m_agentFleet, &AgentFleetPanel::newAgentRequested, this, &MainWindow::onNewChat);
    connect(m_agentFleet, &AgentFleetPanel::deleteRequested, this, [this](const QString &sid) {
        m_chatPanel->deleteSession(sid);
        rebuildFleetPanel();
    });
    connect(m_agentFleet, &AgentFleetPanel::exportAndDeleteRequested, this, [this](const QString &sid) {
        m_chatPanel->exportChatHistory(sid);
        m_chatPanel->deleteSession(sid);
        rebuildFleetPanel();
    });
    connect(m_agentFleet, &AgentFleetPanel::deleteAllExceptTodayRequested, this, [this]() {
        QDate today = QDate::currentDate();
        auto summaries = m_chatPanel->agentSummaries();
        QList<AgentSummary> toDelete;
        for (const auto &s : summaries) {
            if (s.updatedAt <= 0 || QDateTime::fromSecsSinceEpoch(s.updatedAt).date() != today)
                toDelete.append(s);
        }
        if (toDelete.isEmpty()) return;
        auto answer = QMessageBox::question(
            this, "Keep Only Today's Chats",
            QString("Permanently delete %1 chat(s) from before today?").arg(toDelete.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        for (const auto &s : toDelete)
            m_chatPanel->deleteSessionNoConfirm(s.sessionId);
        rebuildFleetPanel();
    });
    connect(m_agentFleet, &AgentFleetPanel::deleteAllRequested, this, [this]() {
        auto summaries = m_chatPanel->agentSummaries();
        if (summaries.isEmpty()) return;
        auto answer = QMessageBox::question(
            this, "Delete All Chats",
            QString("Permanently delete all %1 chats?").arg(summaries.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        for (const auto &s : summaries)
            m_chatPanel->deleteSessionNoConfirm(s.sessionId);
        rebuildFleetPanel();
    });
    connect(m_agentFleet, &AgentFleetPanel::deleteOlderThanDayRequested, this, [this]() {
        qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 86400;
        auto summaries = m_chatPanel->agentSummaries();
        QList<AgentSummary> toDelete;
        for (const auto &s : summaries) {
            if (s.updatedAt > 0 && s.updatedAt < cutoff)
                toDelete.append(s);
        }
        if (toDelete.isEmpty()) return;
        auto answer = QMessageBox::question(
            this, "Delete Old Chats",
            QString("Permanently delete %1 chat(s) older than 1 day?").arg(toDelete.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        for (const auto &s : toDelete)
            m_chatPanel->deleteSessionNoConfirm(s.sessionId);
        rebuildFleetPanel();
    });

    // Rebuild fleet when sessions change
    connect(m_chatPanel, &ChatPanel::sessionListChanged, this, &MainWindow::rebuildFleetPanel);

    // Update fleet card when processing state changes
    connect(m_chatPanel, &ChatPanel::processingChanged, this, [this](bool) {
        rebuildFleetPanel();
    });

    // Update fleet card activity in real-time
    connect(m_chatPanel, &ChatPanel::agentActivityChanged, this,
            [this](const QString &sid, const QString &activity) {
        AgentSummary s;
        s.sessionId = sid;
        s.activity = activity;
        // Get full summary from chat panel for other fields
        for (const auto &agent : m_chatPanel->agentSummaries()) {
            if (agent.sessionId == sid) {
                s = agent;
                s.activity = activity;
                break;
            }
        }
        m_agentFleet->updateAgent(s);
    });

    // Sync fleet selection when chat tab changes
    connect(m_chatPanel, &ChatPanel::activeSessionChanged, this, [this](const QString &sid) {
        m_agentFleet->setSelectedAgent(sid);
        m_effectsPanel->setCurrentSession(sid);
        m_diffEngine->setCurrentSessionId(sid);

        // Sync current turn ID
        for (const auto &s : m_chatPanel->agentSummaries()) {
            if (s.sessionId == sid) {
                m_effectsPanel->setCurrentTurnId(s.turnCount);
                break;
            }
        }

        // If effects panel has no data for this session, load from history
        if (!m_effectsPanel->hasChangesForSession(sid)) {
            auto changes = m_chatPanel->extractFileChangesFromHistory(sid);
            if (!changes.isEmpty())
                m_effectsPanel->populateFromHistory(sid, changes);
        }

        // Load turn timestamps for this session
        auto timestamps = m_chatPanel->turnTimestampsForSession(sid);
        if (!timestamps.isEmpty())
            m_effectsPanel->setTurnTimestamps(timestamps);
    });

    // Historical effects from restored sessions
    connect(m_chatPanel, &ChatPanel::historicalEffectsReady, this,
            [this](const QString &sessionId, const QList<FileChange> &changes) {
        m_effectsPanel->populateFromHistory(sessionId, changes);
    });

    // Sync turn ID to effects panel when a new turn starts
    connect(m_chatPanel, &ChatPanel::turnStarted, this,
            [this](const QString &, int turnId) {
        m_effectsPanel->setCurrentTurnId(turnId);
        m_effectsPanel->setHighlightedTurn(turnId);
    });

    // Chat scroll → effects panel turn highlight
    connect(m_chatPanel, &ChatPanel::visibleTurnChanged, this,
            [this](const QString &, int turnId) {
        m_effectsPanel->setHighlightedTurn(turnId);
    });

    // Turn timestamps from restored sessions
    connect(m_chatPanel, &ChatPanel::turnTimestampsReady, this,
            [this](const QString &, const QMap<int, qint64> &timestamps) {
        m_effectsPanel->setTurnTimestamps(timestamps);
    });

    // Effects panel turn click → scroll chat to that turn
    connect(m_effectsPanel, &EffectsPanel::turnClicked, this, [this](int turnId) {
        m_chatPanel->scrollToTurn(turnId);
    });

    // ── Mission Control: Effects Panel wiring ──
    wireEffectsPanel();

    // Cmd+E: toggle Manager/Editor view
    auto *toggleModeShortcut = new QShortcut(QKeySequence("Ctrl+E"), this);
    connect(toggleModeShortcut, &QShortcut::activated, this, [this] {
        switchToMode(m_viewMode == ViewMode::Manager ? ViewMode::Editor : ViewMode::Manager);
    });

    // Esc: return to Manager View, or dismiss inline code viewer
    auto *escShortcut = new QShortcut(QKeySequence("Escape"), this);
    connect(escShortcut, &QShortcut::activated, this, [this] {
        if (m_viewMode == ViewMode::Editor) {
            switchToMode(ViewMode::Manager);
        } else {
            dismissInlineFilePreview();
        }
    });
}

void MainWindow::syncEditorContextToChat()
{
    if (!m_codeViewer || !m_chatPanel) return;
    m_chatPanel->inputBar()->setOpenFiles(m_codeViewer->openFiles());
}

void MainWindow::onInlineEdit()
{
    if (m_codeViewer && m_codeViewer->isVisible()) {
        if (!m_codeViewer->selectedText().isEmpty())
            m_codeViewer->showInlineEditBar();
    }
}

void MainWindow::executeInlineEdit(const QString &filePath, const QString &selectedCode,
                                   const QString &instruction, int startLine, int endLine,
                                   const QString &modelId)
{
    // Cancel any in-flight inline edit
    if (m_inlineEditProcess) {
        m_inlineEditProcess->cancel();
        m_inlineEditProcess->deleteLater();
        m_inlineEditProcess = nullptr;
    }

    m_inlineEditFile = filePath;
    // Snapshot original content so we can hard-revert on reject
    m_inlineEditOriginalContent = m_codeViewer->fileContent();

    // Determine language hint from file extension
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    static const QMap<QString,QString> extLang = {
        {"cpp","cpp"}, {"cxx","cpp"}, {"cc","cpp"}, {"c","c"},
        {"h","cpp"}, {"hpp","cpp"},
        {"py","python"}, {"js","javascript"}, {"ts","typescript"},
        {"rs","rust"}, {"go","go"}, {"java","java"},
        {"swift","swift"}, {"kt","kotlin"}, {"rb","ruby"},
        {"sh","bash"}, {"json","json"}, {"md","markdown"},
        {"html","html"}, {"css","css"}, {"sql","sql"},
    };
    QString lang = extLang.value(ext, ext);

    // Build a focused prompt with full file context and exact line range
    QString relPath = fi.fileName();
    if (!m_workspacePath.isEmpty() && filePath.startsWith(m_workspacePath))
        relPath = filePath.mid(m_workspacePath.length() + 1);

    QString prompt = QStringLiteral(
        "Edit the file `%1`. Here is the full current content:\n\n"
        "```%2\n%3\n```\n\n"
        "The user selected lines %4\u2013%5:\n\n"
        "```%2\n%6\n```\n\n"
        "Instruction: %7\n\n"
        "Use the Edit tool to apply this change. "
        "Modify only the selected region. Do not add comments or explanations.")
        .arg(relPath, lang, m_inlineEditOriginalContent)
        .arg(startLine).arg(endLine)
        .arg(selectedCode, instruction);

    // Create a dedicated process — keeps chat history clean
    m_inlineEditProcess = new ClaudeProcess(this);
    m_inlineEditProcess->setWorkingDirectory(m_workspacePath);
    m_inlineEditProcess->setModel(modelId);
    m_inlineEditProcess->setMode("agent");

    // --- Wire StreamParser: detect Edit/Write tool calls → show inline diff ---
    auto *parser = m_inlineEditProcess->streamParser();

    connect(parser, &StreamParser::toolUseStarted, this,
            [this](const QString &toolName, const QString &, const nlohmann::json &input) {
        QString fp = JsonUtils::getString(input, "file_path");
        if (fp.isEmpty()) return;
        if (!QFileInfo(fp).isAbsolute() && !m_workspacePath.isEmpty())
            fp = m_workspacePath + "/" + fp;

        if ((toolName == "Edit" || toolName == "StrReplace") && input.contains("old_string")) {
            QString oldStr = JsonUtils::getString(input, "old_string");
            QString newStr = JsonUtils::getString(input, "new_string");

            // Find the line where oldStr starts — compute from old content before reload
            int editLine = 0;
            if (!oldStr.isEmpty()) {
                int pos = m_inlineEditOriginalContent.indexOf(oldStr);
                if (pos >= 0)
                    editLine = m_inlineEditOriginalContent.left(pos).count('\n');
            }

            // showInlineDiff handles reload internally (150ms timer + forceReload)
            m_codeViewer->showInlineDiff(fp, oldStr, newStr, editLine);
        }
        // Write tool: showInlineDiff handles forceReload internally, just ignore here
    });

    connect(m_inlineEditProcess, &ClaudeProcess::finished, this,
            [this](int exitCode) {
        m_statusProcessing->setText("");
        if (m_inlineEditProcess) {
            m_inlineEditProcess->deleteLater();
            m_inlineEditProcess = nullptr;
        }
        if (exitCode == 0)
            m_codeViewer->setInlineEditReviewMode();
        else {
            m_codeViewer->hideInlineEditBar();
            m_inlineEditFile.clear();
            m_inlineEditOriginalContent.clear();
        }
    });

    connect(m_inlineEditProcess, &ClaudeProcess::errorOccurred, this,
            [this](const QString &err) {
        m_statusProcessing->setText("");
        ToastManager::instance().show("Inline edit failed: " + err, ToastType::Error, 4000);
        if (m_inlineEditProcess) {
            m_inlineEditProcess->deleteLater();
            m_inlineEditProcess = nullptr;
        }
        m_codeViewer->hideInlineEditBar();
        m_inlineEditFile.clear();
        m_inlineEditOriginalContent.clear();
    });

    // Switch bar to processing spinner (keep bar visible for feedback)
    m_codeViewer->setInlineEditProcessing();
    m_statusProcessing->setText("\u25cf Editing...");
    m_inlineEditProcess->sendMessage(prompt);
}

void MainWindow::setupStatusBar()
{
    m_statusFile       = new QLabel("No file open", this);
    m_statusBranch     = new QLabel("", this);
    m_statusModel      = new QLabel("", this);
    m_statusProcessing = new QLabel("", this);
    m_statusProcessing->setMinimumWidth(100);
    m_statusProcessing->setText("\u25CB Ready");

    statusBar()->addWidget(m_statusFile, 1);
    statusBar()->addPermanentWidget(m_statusModel);
    statusBar()->addPermanentWidget(m_statusBranch);
    statusBar()->addPermanentWidget(m_statusProcessing);

    // --- View toggle buttons (right side of status bar) ---
    auto makeToggle = [this](const QString &label, const QString &tip) -> QPushButton* {
        auto *btn = new QPushButton(label, this);
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setChecked(true);
        btn->setFixedHeight(20);
        return btn;
    };

    // -- Mode switch button (always visible, text changes per mode) --
    m_toggleMode = makeToggle("Editor", "Toggle Manager/Editor View (Ctrl+E)");
    m_toggleMode->setChecked(false);

    // -- AGM mode buttons --
    m_toggleAgents = makeToggle("Agents", "Toggle Agent Fleet Panel");
    m_toggleAgents->setChecked(true);
    m_toggleEffects = makeToggle("Effects", "Toggle Effects Panel");
    m_toggleEffects->setChecked(true);

    // -- Editor mode buttons --
    m_toggleTree = makeToggle("Explorer", "Toggle Workspace (Ctrl+1)");
    m_toggleTree->setChecked(false);
    m_toggleChat = makeToggle("Chat", "Toggle Chat (Ctrl+3)");
    m_toggleChat->setChecked(true);

    statusBar()->addPermanentWidget(m_toggleMode);
    statusBar()->addPermanentWidget(m_toggleAgents);   // AGM mode
    statusBar()->addPermanentWidget(m_toggleEffects);   // AGM mode
    statusBar()->addPermanentWidget(m_toggleTree);      // Editor mode
    statusBar()->addPermanentWidget(m_toggleChat);      // Editor mode

    // --- Mode switch ---
    connect(m_toggleMode, &QPushButton::clicked, this, [this] {
        switchToMode(m_viewMode == ViewMode::Manager ? ViewMode::Editor : ViewMode::Manager);
    });

    // --- Agents toggle (AGM mode) ---
    connect(m_toggleAgents, &QPushButton::clicked, this, [this] {
        if (m_viewMode != ViewMode::Manager) return;
        bool currentlyUsable = m_agentFleet->isVisible() && m_agentFleet->width() > 10;
        if (currentlyUsable) {
            QList<int> sizes = m_splitter->sizes();
            sizes[3] += sizes[0];
            sizes[0] = 0;
            m_agentFleet->setVisible(false);
            animateSplitterSizes(sizes);
        } else {
            m_agentFleet->setVisible(true);
            int total = m_splitter->width();
            QList<int> sizes = m_splitter->sizes();
            int fleetW = 300;
            sizes[0] = fleetW;
            sizes[3] = total - fleetW - sizes[1] - sizes[2] - sizes[4];
            animateSplitterSizes(sizes);
        }
        updateToggleButtons();
    });

    // --- Effects toggle (AGM mode) ---
    connect(m_toggleEffects, &QPushButton::clicked, this, [this] {
        if (m_viewMode != ViewMode::Manager) return;
        bool currentlyUsable = m_effectsPanel->isVisible() && m_effectsPanel->width() > 10;
        if (currentlyUsable) {
            QList<int> sizes = m_splitter->sizes();
            sizes[3] += sizes[4];
            sizes[4] = 0;
            m_effectsPanel->setVisible(false);
            animateSplitterSizes(sizes);
        } else {
            m_effectsPanel->setVisible(true);
            int total = m_splitter->width();
            QList<int> sizes = m_splitter->sizes();
            int effectsW = 300;
            sizes[4] = effectsW;
            sizes[3] = total - sizes[0] - sizes[1] - sizes[2] - effectsW;
            animateSplitterSizes(sizes);
        }
        updateToggleButtons();
    });

    // --- Explorer toggle (Editor mode) ---
    connect(m_toggleTree, &QPushButton::clicked, this, [this] {
        if (m_viewMode != ViewMode::Editor) return;
        bool currentlyUsable = m_leftTabs->isVisible() && m_leftTabs->width() > 10;
        m_leftTabs->setVisible(!currentlyUsable);
        if (!currentlyUsable) {
            QList<int> sizes = m_splitter->sizes();
            if (sizes.size() >= 5 && sizes[1] < 50) {
                sizes[1] = kTreePanelWidth + 30;
                animateSplitterSizes(sizes);
            }
        }
        updateToggleButtons();
    });

    // --- Chat toggle (Editor mode) ---
    connect(m_toggleChat, &QPushButton::clicked, this, [this] {
        if (m_viewMode != ViewMode::Editor) return;
        bool currentlyUsable = m_chatPanel->isVisible() && m_chatPanel->width() > 10;
        if (currentlyUsable) {
            m_chatPanel->setVisible(false);
        } else {
            m_chatPanel->setVisible(true);
            QList<int> sizes = m_splitter->sizes();
            if (sizes.size() >= 5 && sizes[3] < 50) {
                int total = m_splitter->width();
                sizes[3] = static_cast<int>(total * kChatFraction);
                animateSplitterSizes(sizes);
            }
        }
        updateToggleButtons();
    });

    // --- Status bar signal connections ---
    connect(m_gitManager, &GitManager::branchChanged, this, [this](const QString &branch) {
        m_statusBranch->setText(QStringLiteral("\u2387 %1").arg(branch));
    });

    connect(m_chatPanel->modelSelector(), &ModelSelector::modelChanged, this,
            [this](const QString &) {
        m_statusModel->setText(m_chatPanel->modelSelector()->currentModelLabel());
    });

    connect(m_chatPanel, &ChatPanel::processingChanged, this, [this](bool processing) {
        auto &pal = ThemeManager::instance().palette();
        if (processing) {
            m_statusProcessing->setStyleSheet(
                QStringLiteral("QLabel { color: %1; }").arg(pal.teal.name()));
            m_statusProcessing->setText("\u25CF Processing");
        } else {
            m_statusProcessing->setStyleSheet(
                QStringLiteral("QLabel { color: %1; }").arg(pal.text_faint.name()));
            m_statusProcessing->setText("\u25CB Ready");
        }
    });
}

void MainWindow::setupMenuBar()
{
    auto *fileMenu = menuBar()->addMenu("&File");

    auto *openAction = fileMenu->addAction("&Open Workspace...");
    openAction->setShortcut(QKeySequence("Ctrl+O"));
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenWorkspace);

    auto *newChatAction = fileMenu->addAction("&New Chat");
    newChatAction->setShortcut(QKeySequence("Ctrl+N"));
    connect(newChatAction, &QAction::triggered, this, &MainWindow::onNewChat);

    fileMenu->addSeparator();

    auto *saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence("Ctrl+S"));
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveFile);

    auto *saveAllAction = fileMenu->addAction("Save &All");
    saveAllAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveAllAction, &QAction::triggered, this, &MainWindow::onSaveAllFiles);

    fileMenu->addSeparator();

    auto *quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence("Ctrl+Q"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto *editMenu = menuBar()->addMenu("&Edit");

    auto *undoAction = editMenu->addAction("&Undo");
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, m_codeViewer, &CodeViewer::undo);

    auto *redoAction = editMenu->addAction("&Redo");
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, m_codeViewer, &CodeViewer::redo);

    editMenu->addSeparator();

    auto *cutAction = editMenu->addAction("Cu&t");
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, m_codeViewer, &CodeViewer::cut);

    auto *copyAction = editMenu->addAction("&Copy");
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, [this] {
        auto *w = QApplication::focusWidget();
        if (auto *te = qobject_cast<QTextEdit *>(w))
            te->copy();
        else if (auto *lb = qobject_cast<QLabel *>(w)) {
            if (lb->hasSelectedText())
                QApplication::clipboard()->setText(lb->selectedText());
        } else
            m_codeViewer->copy();
    });

    auto *pasteAction = editMenu->addAction("&Paste");
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, m_codeViewer, &CodeViewer::paste);

    editMenu->addSeparator();

    auto *inlineEditAction = editMenu->addAction("&Inline Edit");
    inlineEditAction->setShortcut(QKeySequence("Ctrl+K"));
    connect(inlineEditAction, &QAction::triggered, this, &MainWindow::onInlineEdit);

    editMenu->addSeparator();

    auto *settingsAction = editMenu->addAction("&Settings...");
    settingsAction->setShortcut(QKeySequence("Ctrl+,"));
    settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dlg(this);
        dlg.exec();
    });

    auto *gitMenu = menuBar()->addMenu("&Git");

    auto *gitRefreshAction = gitMenu->addAction("&Refresh Status");
    gitRefreshAction->setShortcut(QKeySequence("Ctrl+Shift+G"));
    connect(gitRefreshAction, &QAction::triggered, this, &MainWindow::onGitRefresh);

    auto *gitStageAllAction = gitMenu->addAction("Stage &All");
    connect(gitStageAllAction, &QAction::triggered, this, [this] {
        m_gitManager->stageAll();
    });

    auto *gitUnstageAllAction = gitMenu->addAction("&Unstage All");
    connect(gitUnstageAllAction, &QAction::triggered, this, [this] {
        m_gitManager->unstageAll();
    });

    gitMenu->addSeparator();

    auto *gitCommitAction = gitMenu->addAction("&Commit...");
    connect(gitCommitAction, &QAction::triggered, this, [this] {
        if (m_viewMode == ViewMode::Manager)
            switchToMode(ViewMode::Editor);
        m_leftTabs->setVisible(true);
        m_leftTabs->setCurrentWidget(m_gitPanel);
    });

    gitMenu->addSeparator();

    auto *gitDiscardAction = gitMenu->addAction("&Discard All Changes");
    connect(gitDiscardAction, &QAction::triggered, this, [this] {
        auto result = QMessageBox::warning(
            this, "Discard All Changes",
            "Discard ALL working tree changes and delete untracked files?\n\n"
            "This cannot be undone.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result == QMessageBox::Yes)
            m_gitManager->discardAll();
    });

    auto *viewMenu = menuBar()->addMenu("&View");

    auto *searchAction = viewMenu->addAction("&Search in Files");
    searchAction->setShortcut(QKeySequence("Ctrl+Shift+F"));
    connect(searchAction, &QAction::triggered, this, [this] {
        if (m_viewMode == ViewMode::Manager)
            switchToMode(ViewMode::Editor);
        m_leftTabs->setVisible(true);
        m_leftTabs->setCurrentWidget(m_searchPanel);
        updateToggleButtons();
    });

    auto *toggleTermAction = viewMenu->addAction("Toggle &Terminal");
    toggleTermAction->setShortcut(QKeySequence("Ctrl+`"));
    connect(toggleTermAction, &QAction::triggered, this, &MainWindow::onToggleTerminal);

    viewMenu->addSeparator();

    auto *themeMenu = viewMenu->addMenu("&Theme");
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);

    struct ThemeEntry { QString key; QString label; };
    for (const auto &entry : std::initializer_list<ThemeEntry>{
            {"cursor", "Cursor Dark"},
            {"mocha", "Mocha"}, {"macchiato", "Macchiato"},
            {"frappe", QString::fromUtf8("Frapp\xc3\xa9")}, {"latte", "Latte"}}) {
        auto *action = themeMenu->addAction(entry.label);
        action->setCheckable(true);
        action->setData(entry.key);
        action->setChecked(entry.key == ThemeManager::instance().currentThemeName());
        m_themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [key = entry.key] {
            ThemeManager::instance().setTheme(key);
        });
    }

    auto *termMenu = menuBar()->addMenu("T&erminal");

    auto *newTermAction = termMenu->addAction("&New Terminal");
    newTermAction->setShortcut(QKeySequence("Ctrl+Shift+`"));
    connect(newTermAction, &QAction::triggered, this, &MainWindow::onNewTerminal);

    auto *clearTermAction = termMenu->addAction("&Clear");
    connect(clearTermAction, &QAction::triggered, this, &MainWindow::onClearTerminal);
}

void MainWindow::updateToggleButtons()
{
    bool isManager = (m_viewMode == ViewMode::Manager);

    // Mode switch button — text shows the target mode
    m_toggleMode->setChecked(!isManager);
    m_toggleMode->setText(isManager ? "Editor" : "AGM");

    // AGM mode buttons
    m_toggleAgents->setVisible(isManager);
    m_toggleAgents->setChecked(m_agentFleet->isVisible() && m_agentFleet->width() > 10);
    m_toggleEffects->setVisible(isManager);
    m_toggleEffects->setChecked(m_effectsPanel->isVisible() && m_effectsPanel->width() > 10);

    // Editor mode buttons
    m_toggleTree->setVisible(!isManager);
    m_toggleTree->setChecked(m_leftTabs->isVisible() && m_leftTabs->width() > 10);
    m_toggleChat->setVisible(!isManager);
    m_toggleChat->setChecked(m_chatPanel->isVisible() && m_chatPanel->width() > 10);
}

void MainWindow::animateSplitterSizes(const QList<int> &targetSizes, int durationMs)
{
    if (m_splitterAnim) {
        m_splitterAnim->stop();
        m_splitterAnim->deleteLater();
        m_splitterAnim = nullptr;
    }

    QList<int> startSizes = m_splitter->sizes();
    if (startSizes.size() != targetSizes.size()) {
        m_splitter->setSizes(targetSizes);
        updateToggleButtons();
        return;
    }

    m_splitterAnim = new QVariantAnimation(this);
    m_splitterAnim->setDuration(durationMs);
    m_splitterAnim->setStartValue(0.0);
    m_splitterAnim->setEndValue(1.0);
    m_splitterAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_splitterAnim, &QVariantAnimation::valueChanged, this,
            [this, startSizes, targetSizes](const QVariant &v) {
        double t = v.toDouble();
        QList<int> sizes;
        for (int i = 0; i < startSizes.size(); ++i)
            sizes.append(startSizes[i] + static_cast<int>((targetSizes[i] - startSizes[i]) * t));
        m_splitter->setSizes(sizes);
    });

    connect(m_splitterAnim, &QVariantAnimation::finished, this, [this] {
        updateToggleButtons();
        m_splitterAnim->deleteLater();
        m_splitterAnim = nullptr;
    });

    m_splitterAnim->start();
}

void MainWindow::switchToMode(ViewMode mode)
{
    if (m_viewMode == mode) return;
    m_viewMode = mode;

    int total = m_splitter->width();

    if (mode == ViewMode::Editor) {
        // Transition to Editor View
        // Hide agent fleet + effects, show left tabs + code viewer
        m_agentFleet->hide();
        m_effectsPanel->hide();
        m_leftTabs->show();
        m_centerSplitter->show();
        m_codeViewer->show();

        // [Fleet=0, LeftTabs=210, Center=flex, Chat=320, Effects=0]
        int treeW = 210;
        int chatW = 320;
        int centerW = total - treeW - chatW;
        animateSplitterSizes({0, treeW, centerW, chatW, 0}, 250);

        m_splitter->setStretchFactor(0, 0);
        m_splitter->setStretchFactor(1, 0);
        m_splitter->setStretchFactor(2, 1);
        m_splitter->setStretchFactor(3, 0);
        m_splitter->setStretchFactor(4, 0);
    } else {
        // Transition to Manager View
        // Show agent fleet + effects + chat, hide left tabs + code viewer
        if (m_fileBar) m_fileBar->hide();
        m_agentFleet->show();
        m_leftTabs->hide();
        m_centerSplitter->hide();
        m_effectsPanel->show();
        m_chatPanel->show();

        // [Fleet=300, LeftTabs=0, Center=0, Chat=flex, Effects=300]
        int fleetW = 300;
        int effectsW = 300;
        int chatW = total - fleetW - effectsW;
        animateSplitterSizes({fleetW, 0, 0, chatW, effectsW}, 250);

        m_splitter->setStretchFactor(0, 0);
        m_splitter->setStretchFactor(1, 0);
        m_splitter->setStretchFactor(2, 0);
        m_splitter->setStretchFactor(3, 1);
        m_splitter->setStretchFactor(4, 0);
    }
    updateToggleButtons();
}

void MainWindow::dismissInlineFilePreview()
{
    if (m_viewMode != ViewMode::Manager || !m_codeViewer->isVisible()) return;

    m_codeViewer->hide();
    m_centerSplitter->hide();
    m_chatPanel->show();
    if (m_fileBar) m_fileBar->hide();

    int total = m_splitter->width();
    int fleetW = m_agentFleet->isVisible() ? m_splitter->sizes()[0] : 0;
    int effectsW = m_effectsPanel->isVisible() ? m_splitter->sizes()[4] : 0;
    int chatW = total - fleetW - effectsW;
    m_splitter->setSizes({fleetW, 0, 0, chatW, effectsW});
}

void MainWindow::rebuildFleetPanel()
{
    auto agents = m_chatPanel->agentSummaries();
    QString selectedId = m_chatPanel->currentSessionId();
    m_agentFleet->rebuild(agents, selectedId);
}

void MainWindow::wireEffectsPanel()
{
    m_effectsPanel->setRootPath(m_workspacePath);

    // When DiffEngine records a file change, update the effects panel
    connect(m_diffEngine, &DiffEngine::sessionFileChanged, this,
            [this](const QString &sessionId, const QString &filePath) {
        FileDiff diff = m_diffEngine->diffForFile(filePath);

        FileChange change;
        change.filePath = filePath;
        change.sessionId = sessionId;
        change.linesAdded = m_diffEngine->linesAddedForFile(filePath);
        change.linesRemoved = m_diffEngine->linesRemovedForFile(filePath);

        if (diff.isNewFile)
            change.type = FileChange::Created;
        else if (diff.isDeleted)
            change.type = FileChange::Deleted;
        else
            change.type = FileChange::Modified;

        m_effectsPanel->onFileChanged(filePath, change);
    });

    // Click file in effects panel → show code viewer, hide chat panel
    connect(m_effectsPanel, &EffectsPanel::fileClicked, this, [this](const QString &filePath) {
        m_centerSplitter->show();
        m_codeViewer->show();
        m_codeViewer->loadFile(filePath);
        m_statusFile->setText(QFileInfo(filePath).fileName());

        FileDiff diff = m_diffEngine->diffForFile(filePath);
        if (!diff.hunks.isEmpty())
            m_codeViewer->showDiff(diff);

        if (m_viewMode == ViewMode::Manager) {
            m_chatPanel->hide();
            int total = m_splitter->width();
            int fleetW = 300;
            int effectsW = 300;
            int centerW = total - fleetW - effectsW;
            m_splitter->setSizes({fleetW, 0, centerW, 0, effectsW});

            showFileBar(QFileInfo(filePath).fileName());
        }

        syncEditorContextToChat();
    });
}

void MainWindow::showFileBar(const QString &fileName)
{
    if (!m_fileBar) {
        m_fileBar = new QWidget(m_codeViewer);
        m_fileBar->setFixedHeight(32);
        m_fileBar->setCursor(Qt::PointingHandCursor);
        auto *barLayout = new QHBoxLayout(m_fileBar);
        barLayout->setContentsMargins(12, 0, 12, 0);
        barLayout->setSpacing(0);
        m_fileBarLabel = new QLabel(m_fileBar);
        m_fileBarLabel->setTextFormat(Qt::RichText);
        barLayout->addWidget(m_fileBarLabel);
        barLayout->addStretch();
        auto &thm = ThemeManager::instance();
        m_fileBar->setStyleSheet(QStringLiteral(
            "QWidget { background: %1; border-bottom: 1px solid %2; }")
            .arg(thm.hex("bg_surface"), thm.hex("border_subtle")));
        m_fileBar->installEventFilter(this);
        m_codeViewer->installEventFilter(this);
    }
    auto &thm = ThemeManager::instance();
    m_fileBarLabel->setText(QStringLiteral(
        "<span style='color:%1;font-size:11px;'>\xe2\x86\x90 Back to Chat</span>"
        "<span style='color:%2;font-size:11px;'>  \xc2\xb7  </span>"
        "<span style='color:%3;font-size:11px;font-weight:500;'>%4</span>")
        .arg(thm.hex("teal"), thm.hex("text_faint"),
             thm.hex("text_primary"), fileName));
    m_fileBar->show();
    // Defer positioning until after layout has been computed
    QTimer::singleShot(0, this, [this]() {
        if (m_fileBar && m_fileBar->isVisible()) {
            m_fileBar->move(0, 0);
            m_fileBar->resize(m_codeViewer->width(), 32);
            m_fileBar->raise();
        }
    });
}

void MainWindow::loadStylesheet()
{
    auto &tm = ThemeManager::instance();

    QString savedTheme = Config::instance().theme();
    tm.setTheme(savedTheme);

    connect(&tm, &ThemeManager::themeChanged, this, &MainWindow::onThemeChanged);
    applyThemeColors();

}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_fileBar && event->type() == QEvent::MouseButtonPress) {
        dismissInlineFilePreview();
        return true;
    }
    if (obj == m_codeViewer && event->type() == QEvent::Resize) {
        if (m_fileBar && m_fileBar->isVisible()) {
            m_fileBar->move(0, 0);
            m_fileBar->resize(m_codeViewer->width(), 32);
            m_fileBar->raise();
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    const auto &pal = ThemeManager::instance().palette();
    MacUtils::applyTitleBarStyle(this, !pal.isLight, pal.bg_surface);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        const auto &pal = ThemeManager::instance().palette();
        MacUtils::applyTitleBarStyle(this, !pal.isLight, pal.bg_surface);
    }
}

void MainWindow::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();

    m_leftTabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: none; background: %1; }"
        "QTabBar { background: %1; border-top: 1px solid %2; border-bottom: none; }"
        "QTabBar::tab { background: transparent; color: %3; border: none; padding: 4px 8px; font-size: 11px; }"
        "QTabBar::tab:selected { color: %4; border-top: 2px solid %5; }"
        "QTabBar::tab:hover:!selected { color: %6; }")
        .arg(p.bg_window.name(), p.border_subtle.name(), p.text_muted.name(),
             p.text_primary.name(), p.teal.name(), p.text_secondary.name()));

    auto toggleStyle = QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: 1px solid transparent; "
        "border-radius: 4px; font-size: 11px; font-weight: 500; padding: 2px 8px; margin: 0 1px; min-width: 56px; }"
        "QPushButton:hover { color: %2; background: %4; }"
        "QPushButton:checked { color: %3; background: %4; border-color: %5; }")
        .arg(p.text_muted.name(), p.text_secondary.name(), p.text_primary.name(),
             p.bg_raised.name(), p.border_standard.name());

    m_toggleMode->setStyleSheet(toggleStyle);
    m_toggleAgents->setStyleSheet(toggleStyle);
    m_toggleEffects->setStyleSheet(toggleStyle);
    m_toggleTree->setStyleSheet(toggleStyle);
    m_toggleChat->setStyleSheet(toggleStyle);

    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar { background: %1; border-top: 1px solid %2; }"
        "QStatusBar::item { border: none; }"
        "QStatusBar QLabel { color: %3; font-size: 11px; padding: 0 10px; "
        "background: transparent; min-height: 24px; }")
        .arg(p.bg_surface.name(), p.border_subtle.name(), p.text_muted.name()));

    if (m_statusProcessing->text().contains("Processing"))
        m_statusProcessing->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(p.teal.name()));
    else
        m_statusProcessing->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(p.text_faint.name()));
}

void MainWindow::onThemeChanged(const QString &name)
{
    applyThemeColors();
    Config::instance().setTheme(name);

    const auto &pal = ThemeManager::instance().palette();
    MacUtils::applyTitleBarStyle(this, !pal.isLight, pal.bg_surface);

    if (m_themeGroup) {
        for (auto *action : m_themeGroup->actions()) {
            action->setChecked(action->data().toString() == name);
        }
    }
}

void MainWindow::openWorkspace(const QString &path)
{
    bool workspaceChanged = !m_workspacePath.isEmpty() && m_workspacePath != path;
    if (workspaceChanged)
        m_chatPanel->closeAllTabs();

    m_workspacePath = path;
    m_workspaceTree->setRootPath(path);
    m_searchPanel->setRootPath(path);
    m_chatPanel->setWorkingDirectory(path);
    m_terminalPanel->setWorkingDirectory(path);
    m_gitManager->setWorkingDirectory(path);
    m_codeViewer->setRootPath(path);
    if (m_telegramBridge)
        m_telegramBridge->setWorkingDirectory(path);
    if (m_daemonClient) {
        m_daemonClient->setWorkingDirectory(path);
        m_daemonClient->registerWorkspace(path);
    }

    if (!m_gitManager->isGitRepo())
        m_gitPanel->showNotARepo();
    else
        m_gitManager->listBranches();

    Config::instance().setLastWorkspace(path);
    setWindowTitle(QStringLiteral("CCCPP - %1").arg(path));

    // Update Mission Control panels
    m_effectsPanel->setRootPath(path);

    syncEditorContextToChat();

    if (m_chatPanel->tabCount() == 0)
        m_chatPanel->newChat();

    // Initial fleet rebuild
    rebuildFleetPanel();

    // Load the most recent session for this workspace into the checkpoint timeline
    auto sessions = m_database->loadSessions();
    QString latestSessionId;
    qint64 latestTime = 0;
    for (const auto &s : sessions) {
        if (s.workspace == path && s.updatedAt > latestTime) {
            latestTime = s.updatedAt;
            latestSessionId = s.sessionId;
        }
    }
}

void MainWindow::onFileSelected(const QString &filePath)
{
    // If in Manager View, switch to Editor View
    if (m_viewMode == ViewMode::Manager)
        switchToMode(ViewMode::Editor);
    else if (!m_codeViewer->isVisible()) {
        m_codeViewer->show();
        m_centerSplitter->show();
    }

    m_codeViewer->loadFile(filePath);
    m_statusFile->setText(QFileInfo(filePath).fileName());
    FileDiff diff = m_diffEngine->diffForFile(filePath);
    if (!diff.hunks.isEmpty())
        m_codeViewer->showDiff(diff);

    syncEditorContextToChat();
}

void MainWindow::onFileChanged(const QString &filePath)
{
    m_workspaceTree->markFileChanged(filePath);
    m_codeViewer->refreshFile(filePath);
    m_gitManager->refreshStatus();
}

void MainWindow::onNewChat()
{
    m_chatPanel->newChat();
}

void MainWindow::onOpenWorkspace()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Open Workspace",
                                                     QDir::homePath());
    if (!dir.isEmpty())
        openWorkspace(dir);
}

void MainWindow::onSaveFile()
{
    m_codeViewer->saveCurrentFile();
}

void MainWindow::onSaveAllFiles()
{
    m_codeViewer->saveAllFiles();
}

void MainWindow::onBeforeTurnBegins()
{
    if (m_codeViewer->hasDirtyTabs())
        m_codeViewer->saveAllFiles();
}

void MainWindow::onToggleTerminal()
{
    bool willShow = !m_terminalPanel->isVisible();
    m_terminalPanel->setVisible(willShow);
    if (willShow && m_terminalPanel->terminalCount() == 0)
        m_terminalPanel->newTerminal();
    updateToggleButtons();
}

void MainWindow::onNewTerminal()
{
    m_terminalPanel->show();
    m_terminalPanel->newTerminal();
    updateToggleButtons();
}

void MainWindow::onClearTerminal()
{
    m_terminalPanel->clearCurrentTerminal();
}

void MainWindow::connectGitSignals()
{
    connect(m_gitManager, &GitManager::statusChanged, m_workspaceTree, &WorkspaceTree::setGitFileEntries);

    connect(m_gitManager, &GitManager::fileDiffReady, this,
            [this](const QString &filePath, bool staged, const GitUnifiedDiff &diff) {
        if (diff.isBinary) {
            QString fullPath = m_workspacePath + "/" + filePath;
            m_codeViewer->loadFile(fullPath);
            return;
        }

        QString fullPath = m_workspacePath + "/" + filePath;

        if (m_viewMode == ViewMode::Manager) {
            switchToMode(ViewMode::Editor);
        } else if (!m_codeViewer->isVisible()) {
            m_codeViewer->show();
            m_centerSplitter->show();
        }

        QString leftLabel = staged ? "HEAD" : "HEAD";
        QString rightLabel = staged ? "Staged" : "Working Tree";
        m_codeViewer->showSplitDiff(fullPath, diff.oldContent, diff.newContent, leftLabel, rightLabel);
    });

    connect(m_gitManager, &GitManager::errorOccurred, this,
            [](const QString &op, const QString &msg) {
        qDebug() << "[cccpp] Git error in" << op << ":" << msg;
        ToastManager::instance().show(
            QStringLiteral("Git %1 failed: %2").arg(op, msg.trimmed().left(80)),
            ToastType::Error, 4000);
    });

    connect(m_gitManager, &GitManager::commitSucceeded, this,
            [](const QString &hash, const QString &msg) {
        qDebug() << "[cccpp] Committed" << hash << ":" << msg;
        ToastManager::instance().show(
            QStringLiteral("Committed %1").arg(hash.left(7)),
            ToastType::Success, 2500);
    });
    connect(m_gitManager, &GitManager::commitFailed, this,
            [](const QString &err) {
        qDebug() << "[cccpp] Commit failed:" << err;
        ToastManager::instance().show(
            QStringLiteral("Commit failed: %1").arg(err.left(60)),
            ToastType::Error, 5000);
    });

    connect(m_gitManager, &GitManager::pushSucceeded, this,
            [](const QString &branch) {
        ToastManager::instance().show(
            QStringLiteral("Pushed %1 to origin").arg(branch),
            ToastType::Success, 2500);
    });
    connect(m_gitManager, &GitManager::pushFailed, this,
            [](const QString &err) {
        ToastManager::instance().show(
            QStringLiteral("Push failed: %1").arg(err.trimmed().left(80)),
            ToastType::Error, 5000);
    });

    connect(m_gitManager, &GitManager::fetchSucceeded, this, [] {
        ToastManager::instance().show("Fetch completed", ToastType::Success, 2000);
    });
    connect(m_gitManager, &GitManager::fetchFailed, this,
            [](const QString &err) {
        ToastManager::instance().show(
            QStringLiteral("Fetch failed: %1").arg(err.trimmed().left(80)),
            ToastType::Error, 5000);
    });

    connect(m_gitManager, &GitManager::checkoutSucceeded, this,
            [](const QString &branch) {
        ToastManager::instance().show(
            QStringLiteral("Switched to %1").arg(branch),
            ToastType::Success, 2000);
    });
    connect(m_gitManager, &GitManager::checkoutFailed, this,
            [](const QString &err) {
        ToastManager::instance().show(
            QStringLiteral("Checkout failed: %1").arg(err.trimmed().left(80)),
            ToastType::Error, 5000);
    });

}

void MainWindow::onGitRefresh()
{
    m_gitManager->refreshStatus();
}

void MainWindow::restoreSessions()
{
    auto sessions = m_database->loadSessions();
    for (const auto &session : sessions) {
        if (session.workspace == m_workspacePath)
            m_sessionMgr->registerSession(session.sessionId, session);
    }
}

void MainWindow::setupTelegram()
{
    auto &cfg = Config::instance();
    if (!cfg.telegramEnabled() || cfg.telegramBotToken().isEmpty())
        return;

    m_daemonClient = new DaemonClient(this);
    m_daemonClient->setSessionManager(m_sessionMgr);
    m_daemonClient->setDatabase(m_database);
    m_daemonClient->setGitManager(m_gitManager);
    m_daemonClient->setWorkingDirectory(m_workspacePath);

    connect(m_daemonClient, &DaemonClient::filesChanged,
            this, &MainWindow::onGitRefresh);

    // Fall back to direct polling only after reconnection is fully exhausted
    connect(m_daemonClient, &DaemonClient::connectionFailed,
            this, &MainWindow::onDaemonConnectionFailed);

    if (m_daemonClient->connectToDaemon()) {
        if (!m_workspacePath.isEmpty())
            m_daemonClient->registerWorkspace(m_workspacePath);
        qDebug() << "[cccpp] Connected to Telegram daemon";
        return;
    }

    // Initial connection failed — connectionFailed signal was already emitted,
    // onDaemonConnectionFailed() will handle the fallback.
}

void MainWindow::onDaemonConnectionFailed()
{
    // Guard against multiple calls
    if (m_telegramApi) return;

    qDebug() << "[cccpp] Daemon unavailable, falling back to direct polling";

    delete m_daemonClient;
    m_daemonClient = nullptr;

    auto &cfg = Config::instance();
    m_telegramApi = new TelegramApi(this);
    m_telegramApi->setToken(cfg.telegramBotToken());
    m_telegramApi->setAllowedUsers(cfg.telegramAllowedUsers());

    m_telegramBridge = new TelegramBridge(m_telegramApi, this);
    m_telegramBridge->setSessionManager(m_sessionMgr);
    m_telegramBridge->setDatabase(m_database);
    m_telegramBridge->setGitManager(m_gitManager);
    m_telegramBridge->setWorkingDirectory(m_workspacePath);
    connect(m_telegramBridge, &TelegramBridge::filesChanged,
            this, &MainWindow::onGitRefresh);

    m_telegramApi->startPolling();
    qDebug() << "[cccpp] Telegram bot polling started (single-instance fallback)";
}
