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
#include "core/SessionManager.h"
#include "core/SnapshotManager.h"
#include "core/DiffEngine.h"
#include "core/Database.h"
#include "core/GitManager.h"
#include "util/Config.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QApplication>
#include <QScreen>
#include <QStatusBar>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QDebug>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("CCCPP - Claude Code C++ UI");

    m_sessionMgr = new SessionManager(this);
    m_snapshotMgr = new SnapshotManager(this);
    m_diffEngine = new DiffEngine(this);
    m_database = new Database(this);
    m_database->open();
    m_gitManager = new GitManager(this);

    ThemeManager::instance().initialize();

    setupUI();
    setupToolBar();
    setupMenuBar();
    setupStatusBar();
    loadStylesheet();
    connectGitSignals();
    ToastManager::instance().setParentWidget(this);

    if (auto *screen = QApplication::primaryScreen()) {
        QSize screenSize = screen->availableSize();
        resize(screenSize.width() * 4 / 5, screenSize.height() * 4 / 5);
    }

    // Initial sizes: tree=150, editor=0 (hidden), chat=rest
    QTimer::singleShot(0, this, [this] {
        m_splitter->setSizes({150, 0, m_splitter->width() - 150});
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
}

MainWindow::~MainWindow() {}

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
    m_chatPanel->setSnapshotManager(m_snapshotMgr);
    m_chatPanel->setDiffEngine(m_diffEngine);
    m_chatPanel->setDatabase(m_database);

    // Left column: tabbed panel with Files, Search, and Git
    m_leftTabs = new QTabWidget(this);
    m_leftTabs->setDocumentMode(true);
    m_leftTabs->setTabPosition(QTabWidget::South);
    m_leftTabs->addTab(m_workspaceTree, "Files");
    m_leftTabs->addTab(m_searchPanel, "Search");
    m_leftTabs->addTab(m_gitPanel, "Git");
    // Styles applied by applyThemeColors()

    // Center column: CodeViewer on top, TerminalPanel on bottom
    m_centerSplitter = new QSplitter(Qt::Vertical, this);
    m_centerSplitter->setHandleWidth(1);
    m_centerSplitter->addWidget(m_codeViewer);
    m_centerSplitter->addWidget(m_terminalPanel);
    m_centerSplitter->setStretchFactor(0, 3);
    m_centerSplitter->setStretchFactor(1, 1);

    // Terminal starts hidden
    m_terminalPanel->hide();

    m_splitter->addWidget(m_leftTabs);
    m_splitter->addWidget(m_centerSplitter);
    m_splitter->addWidget(m_chatPanel);

    // Default: tree=150px, editor hidden, chat takes the rest
    m_codeViewer->hide();
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 3);

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
        m_codeViewer->loadFile(filePath);
        FileDiff diff = m_diffEngine->diffForFile(filePath);
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
            int treeW = 150;
            int editorW = static_cast<int>(total * 0.4);
            int chatW = total - treeW - editorW;
            m_splitter->setSizes({treeW, editorW, chatW});
            updateToggleButtons();
        }
        m_codeViewer->openMarkdown(filePath);
    });
    connect(m_chatPanel, &ChatPanel::aboutToSendMessage,
            this, &MainWindow::onBeforeTurnBegins);

    // Git panel: open files on request
    connect(m_gitPanel, &GitPanel::requestOpenFile, this, [this](const QString &filePath) {
        QString fullPath = m_workspacePath + "/" + filePath;
        onFileSelected(fullPath);
    });

    // Git panel: file clicked -> show split diff
    connect(m_gitPanel, &GitPanel::fileClicked, this, [this](const QString &filePath, bool staged) {
        m_gitManager->requestFileDiff(filePath, staged);
    });
}

void MainWindow::setupStatusBar()
{
    m_statusFile       = new QLabel("No file open", this);
    m_statusBranch     = new QLabel("", this);
    m_statusModel      = new QLabel("", this);
    m_statusProcessing = new QLabel("", this);

    // Left: file name (stretches)
    statusBar()->addWidget(m_statusFile, 1);

    // Right: permanent widgets (rightmost first)
    statusBar()->addPermanentWidget(m_statusModel);
    statusBar()->addPermanentWidget(m_statusBranch);
    statusBar()->addPermanentWidget(m_statusProcessing);

    // Wire git branch updates
    connect(m_gitManager, &GitManager::branchChanged, this, [this](const QString &branch) {
        m_statusBranch->setText(QStringLiteral("\u2387 %1").arg(branch));
    });

    // Wire model selector
    connect(m_chatPanel->modelSelector(), &ModelSelector::modelChanged, this,
            [this](const QString &) {
        m_statusModel->setText(m_chatPanel->modelSelector()->currentModelLabel());
    });

    // Wire processing state from ChatPanel
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
    // --- File menu ---
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

    // --- Edit menu ---
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
    connect(copyAction, &QAction::triggered, m_codeViewer, &CodeViewer::copy);

    auto *pasteAction = editMenu->addAction("&Paste");
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, m_codeViewer, &CodeViewer::paste);

    editMenu->addSeparator();

    auto *settingsAction = editMenu->addAction("&Settings...");
    settingsAction->setShortcut(QKeySequence("Ctrl+,"));
    settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dlg(this);
        dlg.exec();
    });

    // --- Git menu ---
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

    // --- View menu ---
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

    // Theme submenu
    auto *themeMenu = viewMenu->addMenu("&Theme");
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);

    struct ThemeEntry { QString key; QString label; };
    for (const auto &entry : std::initializer_list<ThemeEntry>{
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

    // --- Terminal menu ---
    auto *termMenu = menuBar()->addMenu("T&erminal");

    auto *newTermAction = termMenu->addAction("&New Terminal");
    newTermAction->setShortcut(QKeySequence("Ctrl+Shift+`"));
    connect(newTermAction, &QAction::triggered, this, &MainWindow::onNewTerminal);

    auto *clearTermAction = termMenu->addAction("&Clear");
    connect(clearTermAction, &QAction::triggered, this, &MainWindow::onClearTerminal);
}

void MainWindow::setupToolBar()
{
    m_toolBar = new QToolBar(this);
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setFixedHeight(26);
    // Styled via QSS
    addToolBar(Qt::TopToolBarArea, m_toolBar);

    auto makeToggle = [this](const QString &label, const QString &tip) -> QPushButton* {
        auto *btn = new QPushButton(label, this);
        btn->setFixedSize(24, 18);
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setChecked(true);
        return btn;
    };

    m_toggleTree = makeToggle("\xe2\x96\x8c", "Toggle Workspace (Ctrl+1)");
    m_toggleEditor = makeToggle("\xe2\x96\x88", "Toggle Editor (Ctrl+2)");
    m_toggleChat = makeToggle("\xe2\x96\x90", "Toggle Chat (Ctrl+3)");
    m_toggleTerminal = makeToggle("\xe2\x96\xbc", "Toggle Terminal (Ctrl+`)");
    m_toggleTerminal->setChecked(false);
    m_toggleTerminal->setFixedSize(42, 18);
    m_toggleTerminal->setText("\xe2\x96\xbc tty");
    // Styles applied by applyThemeColors()

    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    spacer->setFixedHeight(1);
    spacer->setStyleSheet("background: transparent;");
    m_toolBar->addWidget(spacer);

    m_toolBar->addWidget(m_toggleTree);
    m_toolBar->addWidget(m_toggleEditor);
    m_toolBar->addWidget(m_toggleTerminal);
    m_toolBar->addWidget(m_toggleChat);

    connect(m_toggleTree, &QPushButton::clicked, this, [this] {
        m_leftTabs->setVisible(!m_leftTabs->isVisible());
        updateToggleButtons();
    });
    connect(m_toggleEditor, &QPushButton::clicked, this, [this] {
        m_codeViewer->setVisible(!m_codeViewer->isVisible());
        updateToggleButtons();
    });
    connect(m_toggleChat, &QPushButton::clicked, this, [this] {
        m_chatPanel->setVisible(!m_chatPanel->isVisible());
        updateToggleButtons();
    });
    connect(m_toggleTerminal, &QPushButton::clicked, this, [this] {
        onToggleTerminal();
    });
}

void MainWindow::updateToggleButtons()
{
    m_toggleTree->setChecked(m_leftTabs->isVisible());
    m_toggleEditor->setChecked(m_codeViewer->isVisible());
    m_toggleChat->setChecked(m_chatPanel->isVisible());
    m_toggleTerminal->setChecked(m_terminalPanel->isVisible());
}

void MainWindow::loadStylesheet()
{
    auto &tm = ThemeManager::instance();
    tm.initialize();

    QString savedTheme = Config::instance().theme();
    tm.setTheme(savedTheme);  // handles "dark" -> "mocha" mapping

    connect(&tm, &ThemeManager::themeChanged, this, &MainWindow::onThemeChanged);
    applyThemeColors();
}

void MainWindow::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();

    // Left tabs (bottom position)
    m_leftTabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: none; background: %1; }"
        "QTabBar { background: %1; border-top: 1px solid %2; border-bottom: none; }"
        "QTabBar::tab { background: transparent; color: %3; border: none; padding: 4px 12px; font-size: 11px; }"
        "QTabBar::tab:selected { color: %4; border-top: 2px solid %5; }"
        "QTabBar::tab:hover:!selected { color: %6; }")
        .arg(p.bg_base.name(), p.border_standard.name(), p.text_muted.name(),
             p.text_primary.name(), p.mauve.name(), p.text_secondary.name()));

    // Toggle buttons in toolbar
    auto toggleStyle = QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none; "
        "border-radius: 4px; font-size: 11px; padding: 0; margin: 0; }"
        "QPushButton:hover { color: %2; }"
        "QPushButton:checked { color: %3; }")
        .arg(p.text_muted.name(), p.text_secondary.name(), p.text_primary.name());

    m_toggleTree->setStyleSheet(toggleStyle);
    m_toggleEditor->setStyleSheet(toggleStyle);
    m_toggleChat->setStyleSheet(toggleStyle);
    m_toggleTerminal->setStyleSheet(
        QStringLiteral(
            "QPushButton { background: transparent; color: %1; border: none; "
            "border-radius: 4px; font-size: 11px; padding: 0 4px; margin: 0; }"
            "QPushButton:hover { color: %2; }"
            "QPushButton:checked { color: %3; }")
        .arg(p.text_muted.name(), p.text_secondary.name(), p.text_primary.name()));

    // Status bar processing label (re-apply if active)
    if (!m_statusProcessing->text().isEmpty())
        m_statusProcessing->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(p.mauve.name()));
}

void MainWindow::onThemeChanged(const QString &name)
{
    applyThemeColors();
    Config::instance().setTheme(name);

    // Update theme menu checkmarks
    if (m_themeGroup) {
        for (auto *action : m_themeGroup->actions()) {
            action->setChecked(action->data().toString() == name);
        }
    }
}

void MainWindow::openWorkspace(const QString &path)
{
    m_workspacePath = path;
    m_workspaceTree->setRootPath(path);
    m_searchPanel->setRootPath(path);
    m_chatPanel->setWorkingDirectory(path);
    m_terminalPanel->setWorkingDirectory(path);
    m_snapshotMgr->setGitManager(m_gitManager);
    m_snapshotMgr->setWorkingDirectory(path);
    m_snapshotMgr->setDatabase(m_database);
    m_gitManager->setWorkingDirectory(path);

    if (!m_gitManager->isGitRepo())
        m_gitPanel->showNotARepo();

    Config::instance().setLastWorkspace(path);
    setWindowTitle(QStringLiteral("CCCPP - %1").arg(path));

    if (m_chatPanel->tabCount() == 0)
        m_chatPanel->newChat();
}

void MainWindow::onFileSelected(const QString &filePath)
{
    // Auto-show editor if hidden
    if (!m_codeViewer->isVisible()) {
        m_codeViewer->show();
        // Set proportions: tree=150, editor=40%, chat=rest
        int total = m_splitter->width();
        int treeW = 150;
        int editorW = static_cast<int>(total * 0.4);
        int chatW = total - treeW - editorW;
        m_splitter->setSizes({treeW, editorW, chatW});
        updateToggleButtons();
    }

    m_codeViewer->loadFile(filePath);
    m_statusFile->setText(QFileInfo(filePath).fileName());
    FileDiff diff = m_diffEngine->diffForFile(filePath);
    if (!diff.hunks.isEmpty())
        m_codeViewer->showDiff(diff);
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
    // Git status -> WorkspaceTree badges
    connect(m_gitManager, &GitManager::statusChanged, m_workspaceTree, &WorkspaceTree::setGitFileEntries);

    // Git diff ready -> show in CodeViewer split view
    connect(m_gitManager, &GitManager::fileDiffReady, this,
            [this](const QString &filePath, bool staged, const GitUnifiedDiff &diff) {
        if (diff.isBinary) {
            // Just load the file normally
            QString fullPath = m_workspacePath + "/" + filePath;
            m_codeViewer->loadFile(fullPath);
            return;
        }

        QString fullPath = m_workspacePath + "/" + filePath;

        // Auto-show editor if hidden
        if (!m_codeViewer->isVisible()) {
            m_codeViewer->show();
            int total = m_splitter->width();
            int treeW = 150;
            int editorW = static_cast<int>(total * 0.5);
            int chatW = total - treeW - editorW;
            m_splitter->setSizes({treeW, editorW, chatW});
            updateToggleButtons();
        }

        QString leftLabel = staged ? "HEAD" : "HEAD";
        QString rightLabel = staged ? "Staged" : "Working Tree";
        m_codeViewer->showSplitDiff(fullPath, diff.oldContent, diff.newContent, leftLabel, rightLabel);
    });

    // Git errors -> status bar / debug
    connect(m_gitManager, &GitManager::errorOccurred, this,
            [](const QString &op, const QString &msg) {
        qDebug() << "[cccpp] Git error in" << op << ":" << msg;
    });

    // Commit feedback
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

    // After SnapshotManager revert, refresh git status
    connect(m_snapshotMgr, &SnapshotManager::revertCompleted, this, [this](int) {
        m_gitManager->refreshStatus();
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
        if (session.workspace == m_workspacePath && !session.sessionId.startsWith("pending-"))
            m_sessionMgr->registerSession(session.sessionId, session);
    }
}
