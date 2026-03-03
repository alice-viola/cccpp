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
        m_splitter->setSizes({kTreePanelWidth, 0, m_splitter->width() - kTreePanelWidth});
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
    m_splitter->setHandleWidth(1);

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

    m_leftTabs = new QTabWidget(this);
    m_leftTabs->setDocumentMode(true);
    m_leftTabs->setTabPosition(QTabWidget::South);
    m_leftTabs->addTab(m_workspaceTree, "Files");
    m_leftTabs->addTab(m_gitPanel, "Git");
    m_leftTabs->addTab(m_searchPanel, "Search");

    m_centerSplitter = new QSplitter(Qt::Vertical, this);
    m_centerSplitter->setHandleWidth(1);
    m_centerSplitter->addWidget(m_codeViewer);
    m_centerSplitter->addWidget(m_terminalPanel);
    m_centerSplitter->setStretchFactor(0, 3);
    m_centerSplitter->setStretchFactor(1, 1);

    m_terminalPanel->hide();

    m_splitter->addWidget(m_leftTabs);
    m_splitter->addWidget(m_centerSplitter);
    m_splitter->addWidget(m_chatPanel);

    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, false);
    m_splitter->setCollapsible(2, false);
    m_leftTabs->setMinimumWidth(100);
    m_chatPanel->setMinimumWidth(200);

    m_codeViewer->hide();
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);

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
        m_codeViewer->loadFile(resolved);
        FileDiff diff = m_diffEngine->diffForFile(resolved);
        if (!diff.hunks.isEmpty())
            m_codeViewer->showDiff(diff);
        if (line > 0)
            m_codeViewer->scrollToLine(line);
    });
    connect(m_chatPanel, &ChatPanel::planFileDetected, this,
            [this](const QString &filePath) {
        if (!m_codeViewer->isVisible()) {
            m_codeViewer->show();
            int total = m_splitter->width();
            int editorW = static_cast<int>(total * kEditorFraction);
            int chatW = total - kTreePanelWidth - editorW;
            m_splitter->setSizes({kTreePanelWidth, editorW, chatW});
            updateToggleButtons();
        }
        m_codeViewer->openMarkdown(filePath);
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

    m_toggleTree = makeToggle("Explorer", "Toggle Workspace (Ctrl+1)");
    m_toggleEditor = makeToggle("Editor", "Toggle Editor (Ctrl+2)");
    m_toggleChat = makeToggle("Chat", "Toggle Chat (Ctrl+3)");
    m_toggleTerminal = makeToggle("Terminal", "Toggle Terminal (Ctrl+`)");
    m_toggleTerminal->setChecked(false);

    statusBar()->addPermanentWidget(m_toggleTree);
    statusBar()->addPermanentWidget(m_toggleEditor);
    statusBar()->addPermanentWidget(m_toggleTerminal);
    statusBar()->addPermanentWidget(m_toggleChat);

    connect(m_toggleTree, &QPushButton::clicked, this, [this] {
        bool currentlyUsable = m_leftTabs->isVisible() && m_leftTabs->width() > 10;
        if (currentlyUsable) {
            m_leftTabs->setVisible(false);
        } else {
            m_leftTabs->setVisible(true);
            QList<int> sizes = m_splitter->sizes();
            if (sizes.size() >= 3 && sizes[0] < 50) {
                sizes[0] = kTreePanelWidth + 30;
                m_splitter->setSizes(sizes);
            }
        }
        updateToggleButtons();
    });
    connect(m_toggleEditor, &QPushButton::clicked, this, [this] {
        bool currentlyUsable = m_codeViewer->isVisible() && m_centerSplitter->width() > 10;
        if (currentlyUsable) {
            m_codeViewer->setVisible(false);
        } else {
            m_codeViewer->setVisible(true);
            QList<int> sizes = m_splitter->sizes();
            if (sizes.size() >= 3 && sizes[1] < 50) {
                int total = m_splitter->width();
                sizes[1] = static_cast<int>(total * kEditorFraction);
                sizes[2] = total - sizes[0] - sizes[1];
                m_splitter->setSizes(sizes);
            }
        }
        updateToggleButtons();
    });
    connect(m_toggleChat, &QPushButton::clicked, this, [this] {
        bool currentlyUsable = m_chatPanel->isVisible() && m_chatPanel->width() > 10;
        if (currentlyUsable) {
            m_chatPanel->setVisible(false);
        } else {
            m_chatPanel->setVisible(true);
            QList<int> sizes = m_splitter->sizes();
            if (sizes.size() >= 3 && sizes[2] < 50) {
                int total = m_splitter->width();
                sizes[2] = static_cast<int>(total * kChatFraction);
                sizes[1] = total - sizes[0] - sizes[2];
                m_splitter->setSizes(sizes);
            }
        }
        updateToggleButtons();
    });
    connect(m_toggleTerminal, &QPushButton::clicked, this, [this] {
        onToggleTerminal();
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
        if (processing) {
            m_statusProcessing->setStyleSheet(
                QStringLiteral("QLabel { color: %1; }").arg(ThemeManager::instance().hex("mauve")));
            m_statusProcessing->setText("\u25CF Processing");
        } else {
            m_statusProcessing->setStyleSheet("");
            m_statusProcessing->clear();
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
    m_toggleTree->setChecked(m_leftTabs->isVisible() && m_leftTabs->width() > 10);
    m_toggleEditor->setChecked(m_codeViewer->isVisible() && m_centerSplitter->width() > 10);
    m_toggleChat->setChecked(m_chatPanel->isVisible() && m_chatPanel->width() > 10);
    m_toggleTerminal->setChecked(m_terminalPanel->isVisible());
}

void MainWindow::loadStylesheet()
{
    auto &tm = ThemeManager::instance();

    QString savedTheme = Config::instance().theme();
    tm.setTheme(savedTheme);

    connect(&tm, &ThemeManager::themeChanged, this, &MainWindow::onThemeChanged);
    applyThemeColors();

}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    const auto &pal = ThemeManager::instance().palette();
    MacUtils::applyTitleBarStyle(this, !pal.isLight, pal.bg_window);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        const auto &pal = ThemeManager::instance().palette();
        MacUtils::applyTitleBarStyle(this, !pal.isLight, pal.bg_window);
    }
}

void MainWindow::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();

    m_leftTabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: none; background: %1; }"
        "QTabBar { background: %1; border-top: 1px solid %2; border-bottom: none; }"
        "QTabBar::tab { background: transparent; color: %3; border: none; padding: 4px 12px; font-size: 11px; }"
        "QTabBar::tab:selected { color: %4; border-top: 2px solid %5; }"
        "QTabBar::tab:hover:!selected { color: %6; }")
        .arg(p.bg_window.name(), p.border_subtle.name(), p.text_muted.name(),
             p.text_primary.name(), p.blue.name(), p.text_secondary.name()));

    auto toggleStyle = QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; "
        "border-radius: 4px; font-size: 11px; font-weight: 500; padding: 2px 12px; margin: 0 1px; }"
        "QPushButton:hover { color: %2; }"
        "QPushButton:checked { color: %3; }")
        .arg(p.text_muted.name(), p.text_secondary.name(), p.text_primary.name());

    m_toggleTree->setStyleSheet(toggleStyle);
    m_toggleEditor->setStyleSheet(toggleStyle);
    m_toggleChat->setStyleSheet(toggleStyle);
    m_toggleTerminal->setStyleSheet(toggleStyle);

    if (!m_statusProcessing->text().isEmpty())
        m_statusProcessing->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(p.mauve.name()));
}

void MainWindow::onThemeChanged(const QString &name)
{
    applyThemeColors();
    Config::instance().setTheme(name);

    const auto &pal = ThemeManager::instance().palette();
    MacUtils::applyTitleBarStyle(this, !pal.isLight, pal.bg_window);

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

    syncEditorContextToChat();

    if (m_chatPanel->tabCount() == 0)
        m_chatPanel->newChat();

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
    if (!m_codeViewer->isVisible()) {
        m_codeViewer->show();
        int total = m_splitter->width();
        int editorW = static_cast<int>(total * kEditorFraction);
        int chatW = total - kTreePanelWidth - editorW;
        m_splitter->setSizes({kTreePanelWidth, editorW, chatW});
        updateToggleButtons();
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

        if (!m_codeViewer->isVisible()) {
            m_codeViewer->show();
            int total = m_splitter->width();
            int editorW = static_cast<int>(total * kEditorFractionGit);
            int chatW = total - kTreePanelWidth - editorW;
            m_splitter->setSizes({kTreePanelWidth, editorW, chatW});
            updateToggleButtons();
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
