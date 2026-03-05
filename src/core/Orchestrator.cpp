#include "core/Orchestrator.h"
#include "ui/ChatPanel.h"
#include "core/PersonalityProfile.h"
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <nlohmann/json.hpp>

// ─── MCP Server Script (embedded) ───────────────────────────────────────────
// Keep in sync with resources/mcp/orchestrator_server.py.
// We embed a version hash so we can detect when the installed copy is outdated.
static const char *MCP_SCRIPT_VERSION = "2.0.0";

static QString mcpScriptPath()
{
    return QDir::homePath() + "/.cccpp/mcp/orchestrator_server.py";
}

static QString mcpServerToolPrefix()
{
    return QStringLiteral("mcp__%1__").arg(Orchestrator::MCP_SERVER_NAME);
}

static QString inboxBaseDir()
{
    return QDir::homePath() + "/.cccpp/inboxes";
}

// ─── ensureMcpServerInstalled ────────────────────────────────────────────────

bool Orchestrator::ensureMcpServerInstalled()
{
    QString scriptPath = mcpScriptPath();
    QDir().mkpath(QFileInfo(scriptPath).absolutePath());

    // Check if script exists and is current version
    bool needsInstall = true;
    if (QFile::exists(scriptPath)) {
        QFile f(scriptPath);
        if (f.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(f.readAll());
            if (content.contains(QStringLiteral("version.: .%1.").arg(MCP_SCRIPT_VERSION)))
                needsInstall = false;
        }
    }

    if (needsInstall) {
        // Copy from bundled resources
        QString srcPath = QCoreApplication::applicationDirPath()
                          + "/../resources/mcp/orchestrator_server.py";
        // Fallback: try relative to source tree (development)
        if (!QFile::exists(srcPath)) {
            QStringList candidates = {
                QDir::currentPath() + "/resources/mcp/orchestrator_server.py",
                QCoreApplication::applicationDirPath() + "/../../resources/mcp/orchestrator_server.py",
            };
            for (const auto &c : candidates) {
                if (QFile::exists(c)) { srcPath = c; break; }
            }
        }

        if (QFile::exists(srcPath)) {
            QFile::remove(scriptPath);
            if (!QFile::copy(srcPath, scriptPath)) {
                qWarning() << "[Orchestrator] Failed to copy MCP script to" << scriptPath;
                return false;
            }
            QFile(scriptPath).setPermissions(
                QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                QFileDevice::ReadOther | QFileDevice::ExeOther);
        } else {
            qWarning() << "[Orchestrator] MCP script source not found at" << srcPath;
            return false;
        }
    }

    // Register in ~/.claude.json
    QString claudeConfigPath = QDir::homePath() + "/.claude.json";
    nlohmann::json config;

    if (QFile::exists(claudeConfigPath)) {
        QFile f(claudeConfigPath);
        if (f.open(QIODevice::ReadOnly)) {
            try {
                config = nlohmann::json::parse(f.readAll().toStdString());
            } catch (...) {
                config = nlohmann::json::object();
            }
        }
    }

    bool needsRegister = true;
    if (config.contains("mcpServers") && config["mcpServers"].is_object()) {
        auto &servers = config["mcpServers"];
        if (servers.contains(MCP_SERVER_NAME) && servers[MCP_SERVER_NAME].is_object()) {
            auto &srv = servers[MCP_SERVER_NAME];
            if (srv.contains("args") && srv["args"].is_array() && !srv["args"].empty()) {
                QString existingPath = QString::fromStdString(srv["args"][0].get<std::string>());
                if (existingPath == scriptPath)
                    needsRegister = false;
            }
        }
    }

    if (needsRegister) {
        if (!config.contains("mcpServers") || !config["mcpServers"].is_object())
            config["mcpServers"] = nlohmann::json::object();

        config["mcpServers"][MCP_SERVER_NAME] = {
            {"command", "python3"},
            {"args", {scriptPath.toStdString()}}
        };

        QFile f(claudeConfigPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::string jsonStr = config.dump(2);
            f.write(jsonStr.c_str(), jsonStr.size());
            f.write("\n", 1);
            qDebug() << "[Orchestrator] Registered MCP server in" << claudeConfigPath;
        } else {
            qWarning() << "[Orchestrator] Failed to write" << claudeConfigPath;
            return false;
        }
    }

    qDebug() << "[Orchestrator] MCP server ready:" << scriptPath;
    return true;
}

// ─── Constructor / Setters ───────────────────────────────────────────────────

Orchestrator::Orchestrator(QObject *parent)
    : QObject(parent)
{
}

void Orchestrator::setChatPanel(ChatPanel *panel) { m_chatPanel = panel; }
void Orchestrator::setWorkspace(const QString &workspace) { m_workspace = workspace; }

// ─── Helpers ─────────────────────────────────────────────────────────────────

QString Orchestrator::generateAgentName(const QString &role)
{
    m_agentCounter++;
    return QStringLiteral("%1-%2").arg(role.toLower()).arg(m_agentCounter);
}

void Orchestrator::cleanupInboxes()
{
    if (m_teamId.isEmpty()) return;

    QString inboxDir = inboxBaseDir() + "/" + m_teamId;
    QDir dir(inboxDir);
    if (dir.exists())
        dir.removeRecursively();

    if (m_inboxWatcher) {
        delete m_inboxWatcher;
        m_inboxWatcher = nullptr;
    }

    m_teamId.clear();
    m_pendingChildren.clear();
}

// ─── Start / Cancel ──────────────────────────────────────────────────────────

QString Orchestrator::start(const QString &goal,
                            const QStringList &contextProfileIds)
{
    if (!m_chatPanel || m_running) return {};

    // Ensure MCP server is installed and registered
    if (!ensureMcpServerInstalled()) {
        qWarning() << "[Orchestrator] MCP server installation failed, proceeding anyway";
    }

    m_running = true;
    m_goal = goal;
    m_contextProfileIds = contextProfileIds;
    m_totalDelegations = 0;
    m_totalRetries = 0;
    m_stepAttempts = 0;
    m_mcpCommandReceived = false;
    m_orchestratorTurnDone = false;
    m_pendingFeedResult.clear();
    m_pendingChildren.clear();
    m_agentCounter = 0;
    m_currentPhase = "planning";
    emit phaseChanged(m_currentPhase);

    // Generate unique team ID for inbox scoping
    m_teamId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString inboxDir = inboxBaseDir() + "/" + m_teamId;
    QDir().mkpath(inboxDir);

    // Watch inbox directory for UI updates (peer messages)
    m_inboxWatcher = new QFileSystemWatcher(this);
    m_inboxWatcher->addPath(inboxDir);
    connect(m_inboxWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &path) {
        // Scan for new message files → emit signal for fleet panel
        QDir dir(path);
        for (const auto &file : dir.entryList({"*.jsonl"}, QDir::Files)) {
            Q_UNUSED(file);
            // Optional: parse last line for UI display
        }
    });

    // Create a new chat session for the orchestrator itself
    m_sessionId = m_chatPanel->newChat();

    // Configure: orchestrator mode + only the orchestrator profile
    m_chatPanel->configureSession(m_sessionId, "orchestrator",
                                   {"specialist-orchestrator"});

    qDebug() << "[Orchestrator] Started with goal:" << goal
             << "session:" << m_sessionId << "team:" << m_teamId;

    // Connect signals using member slots (safe for repeated start() calls)
    connect(m_chatPanel, &ChatPanel::sessionIdChanged,
            this, &Orchestrator::onSessionIdChanged, Qt::UniqueConnection);
    connect(m_chatPanel, &ChatPanel::sessionFinishedProcessing,
            this, &Orchestrator::onSessionFinishedProcessing, Qt::UniqueConnection);
    connect(m_chatPanel, &ChatPanel::mcpOrchestratorToolCalled,
            this, &Orchestrator::onMcpToolCalled, Qt::UniqueConnection);

    // Build and send the initial prompt
    QString initialMessage = QStringLiteral(
        "# Goal\n\n%1\n\n"
        "# Instructions\n\n"
        "You are an autonomous orchestrator. Analyze this goal and begin working toward it.\n\n"
        "You MUST use the provided tools to act. Available tools:\n"
        "- `delegate(role, task)` — assign work to architect/implementer/reviewer/tester\n"
        "- `validate(command, description)` — run a shell command to verify the work\n"
        "- `done(summary)` — report the goal is fully achieved\n"
        "- `fail(reason)` — report unrecoverable failure\n\n"
        "You may call MULTIPLE delegate tools in a single turn to run agents in parallel.\n"
        "For example, after the architect finishes, delegate to both a backend implementer "
        "and a frontend implementer simultaneously.\n\n"
        "For validate, done, and fail — call exactly ONE tool per turn.\n\n"
        "Start by delegating to an architect to design the solution.")
        .arg(goal);

    m_chatPanel->sendMessageToSession(m_sessionId, initialMessage);

    return m_sessionId;
}

void Orchestrator::cancel()
{
    m_running = false;
    m_currentPhase = "cancelled";
    if (m_validationProcess) {
        m_validationProcess->kill();
        m_validationProcess->deleteLater();
        m_validationProcess = nullptr;
    }
    cleanupInboxes();
    emit phaseChanged(m_currentPhase);
}

// ─── MCP Tool Interception (primary path) ────────────────────────────────────

void Orchestrator::onMcpToolCalled(const QString &sessionId,
                                    const QString &toolName,
                                    const nlohmann::json &arguments)
{
    if (!m_running) return;
    if (sessionId != m_sessionId) return;

    // Extract action from tool name: mcp__c3p2-orchestrator__delegate → delegate
    QString prefix = mcpServerToolPrefix();
    if (!toolName.startsWith(prefix)) return;
    QString action = toolName.mid(prefix.length());

    qDebug() << "[Orchestrator] MCP tool called:" << action
             << "args:" << QString::fromStdString(arguments.dump());

    m_mcpCommandReceived = true;
    m_stepAttempts = 0;

    OrchestratorCommand cmd;
    if (action == "delegate") {
        cmd.action = OrchestratorCommand::Delegate;
        cmd.role = QString::fromStdString(arguments.value("role", ""));
        cmd.task = QString::fromStdString(arguments.value("task", ""));
    } else if (action == "validate") {
        cmd.action = OrchestratorCommand::Validate;
        cmd.command = QString::fromStdString(arguments.value("command", ""));
        cmd.description = QString::fromStdString(arguments.value("description", "Validation"));
    } else if (action == "done") {
        cmd.action = OrchestratorCommand::Done;
        cmd.summary = QString::fromStdString(arguments.value("summary", ""));
    } else if (action == "fail") {
        cmd.action = OrchestratorCommand::Fail;
        cmd.reason = QString::fromStdString(arguments.value("reason", ""));
    } else {
        qWarning() << "[Orchestrator] Unknown MCP tool action:" << action;
        return;
    }

    executeCommand(cmd);
}

// ─── Session ID Tracking ─────────────────────────────────────────────────────

void Orchestrator::onSessionIdChanged(const QString &oldId, const QString &newId)
{
    if (!m_running) return;
    if (oldId == m_sessionId) {
        qDebug() << "[Orchestrator] Session ID changed:" << oldId << "->" << newId;
        m_sessionId = newId;
    }
    // Re-key pending children map if a child session ID changed
    if (m_pendingChildren.contains(oldId)) {
        auto pc = m_pendingChildren.take(oldId);
        pc.sessionId = newId;
        m_pendingChildren[newId] = pc;
    }
}

// ─── Fallback: Text Parsing (when MCP tools not available) ───────────────────

void Orchestrator::onSessionFinishedProcessing(const QString &sessionId)
{
    if (!m_running) return;
    if (sessionId != m_sessionId) return;

    m_orchestratorTurnDone = true;

    // If a validation (or other fast operation) already produced a result
    // while the Claude CLI was still running, send it now.
    if (!m_pendingFeedResult.isEmpty()) {
        QString result = m_pendingFeedResult;
        m_pendingFeedResult.clear();
        m_mcpCommandReceived = false;
        m_orchestratorTurnDone = false;  // reset for the new turn we're about to start
        qDebug() << "[Orchestrator] Sending deferred feed result";
        m_chatPanel->sendMessageToSession(m_sessionId, result);
        return;
    }

    // If MCP tool was already handled this turn, skip text parsing
    if (m_mcpCommandReceived) {
        m_mcpCommandReceived = false;  // reset for next turn
        return;
    }

    // Fallback: try text parsing (MCP server may not be available)
    QTimer::singleShot(100, this, &Orchestrator::onOrchestratorTurnFinished);
}

void Orchestrator::onOrchestratorTurnFinished()
{
    if (!m_running) return;

    // Still waiting for delegated children to finish
    for (const auto &pc : m_pendingChildren) {
        if (!pc.completed) return;
    }

    // Fallback text parsing — only reached if MCP tool call was not intercepted
    QString output = m_chatPanel->sessionFinalOutput(m_sessionId);
    if (output.isEmpty()) return;

    auto cmd = parseCommand(output);

    if (cmd.action == OrchestratorCommand::Unknown) {
        m_stepAttempts++;
        qDebug() << "[Orchestrator] No MCP tool call or command block found, attempt"
                 << m_stepAttempts << "/" << MAX_STEP_ATTEMPTS;
        if (m_stepAttempts >= MAX_STEP_ATTEMPTS) {
            m_running = false;
            m_currentPhase = "failed";
            emit phaseChanged(m_currentPhase);
            emit failed("Orchestrator failed to produce a valid tool call after "
                        + QString::number(MAX_STEP_ATTEMPTS) + " attempts.");
            return;
        }
        feedResult("ERROR: You must call one of the provided tools (delegate, validate, done, fail). "
                   "Do not output plain text — use a tool call.");
        return;
    }
    m_stepAttempts = 0;

    executeCommand(cmd);
}

OrchestratorCommand Orchestrator::parseCommand(const QString &text) const
{
    OrchestratorCommand cmd;
    cmd.action = OrchestratorCommand::Unknown;

    // Find ```command ... ``` block (fallback when MCP not available)
    int start = text.lastIndexOf("```command");
    if (start < 0) {
        start = text.lastIndexOf("```json");
        if (start < 0) return cmd;
    }

    start = text.indexOf('\n', start);
    if (start < 0) return cmd;
    start++;

    int end = text.indexOf("```", start);
    if (end < 0) return cmd;

    QString jsonStr = text.mid(start, end - start).trimmed();

    try {
        auto j = nlohmann::json::parse(jsonStr.toStdString());
        QString action = QString::fromStdString(j.value("action", ""));

        if (action == "delegate") {
            cmd.action = OrchestratorCommand::Delegate;
            cmd.role = QString::fromStdString(j.value("role", ""));
            cmd.task = QString::fromStdString(j.value("task", ""));
        } else if (action == "validate") {
            cmd.action = OrchestratorCommand::Validate;
            std::string cmdStr = j.value("command", "");
            if (cmdStr.empty()) cmdStr = j.value("task", "");
            if (cmdStr.empty()) cmdStr = j.value("cmd", "");
            cmd.command = QString::fromStdString(cmdStr);
            cmd.description = QString::fromStdString(j.value("description", "Validation"));
        } else if (action == "done") {
            cmd.action = OrchestratorCommand::Done;
            cmd.summary = QString::fromStdString(j.value("summary", ""));
        } else if (action == "fail") {
            cmd.action = OrchestratorCommand::Fail;
            cmd.reason = QString::fromStdString(j.value("reason", ""));
        }
    } catch (const std::exception &e) {
        qWarning() << "[Orchestrator] JSON parse error:" << e.what();
    }

    return cmd;
}

// ─── Command Execution (shared by MCP and fallback paths) ────────────────────

void Orchestrator::executeCommand(const OrchestratorCommand &cmd)
{
    switch (cmd.action) {
    case OrchestratorCommand::Delegate: {
        if (m_totalDelegations >= MAX_TOTAL_DELEGATIONS) {
            feedResult(QStringLiteral(
                "ERROR: Maximum delegation limit (%1) reached. "
                "You must finish with 'done' or 'fail' now.")
                .arg(MAX_TOTAL_DELEGATIONS));
            return;
        }
        m_totalDelegations++;

        QString agentName = generateAgentName(cmd.role);
        m_currentPhase = "delegating to " + cmd.role;
        emit phaseChanged(m_currentPhase);
        emit delegationStarted(cmd.role, cmd.task);

        QString profileId = "specialist-" + cmd.role.toLower();

        // Get orchestrator's context summary for the child
        QString context = m_chatPanel->sessionFinalOutput(m_sessionId);

        // Build teammate list from currently pending (non-completed) children
        QStringList teammates;
        for (const auto &pc : m_pendingChildren) {
            if (!pc.completed)
                teammates << pc.agentName;
        }

        QString childId = m_chatPanel->delegateToChild(
            m_sessionId, cmd.task, context, profileId, m_contextProfileIds,
            agentName, m_teamId, teammates);

        PendingChild pc;
        pc.sessionId = childId;
        pc.role = cmd.role;
        pc.agentName = agentName;
        m_pendingChildren[childId] = pc;

        // Update teammate lists for previously spawned children in this batch.
        // (They were spawned before this agent existed, so they don't know about it yet.
        //  The system prompt is already sent, but the agent can discover via check_inbox.)

        qDebug() << "[Orchestrator] Delegated to" << cmd.role
                 << "agent:" << agentName << "child:" << childId
                 << "pending:" << m_pendingChildren.size();
        break;
    }

    case OrchestratorCommand::Validate: {
        if (cmd.command.trimmed().isEmpty()) {
            feedResult("ERROR: validate requires a non-empty 'command' parameter.");
            return;
        }
        m_currentPhase = "validating: " + cmd.description;
        emit phaseChanged(m_currentPhase);
        emit validationStarted(cmd.command);

        m_validationProcess = new QProcess(this);
        m_validationProcess->setWorkingDirectory(m_workspace);
        m_validationProcess->setProcessChannelMode(QProcess::MergedChannels);

        connect(m_validationProcess, &QProcess::finished, this,
                [this](int exitCode) {
            if (!m_validationProcess) return;
            QString output = QString::fromUtf8(m_validationProcess->readAll());
            if (output.length() > 3000)
                output = output.left(3000) + "\n[...truncated...]";
            m_validationProcess->deleteLater();
            m_validationProcess = nullptr;

            bool passed = (exitCode == 0);
            emit validationResult(passed, output);

            if (passed) {
                feedResult(QStringLiteral(
                    "Validation PASSED (exit code 0).\n\nOutput:\n```\n%1\n```")
                    .arg(output));
            } else {
                m_totalRetries++;
                feedResult(QStringLiteral(
                    "Validation FAILED (exit code %1).\n\nOutput:\n```\n%2\n```")
                    .arg(exitCode).arg(output));
            }
        });

        qDebug() << "[Orchestrator] Running validation:" << cmd.command;
        m_validationProcess->start("/bin/sh", {"-c", cmd.command});

        QTimer::singleShot(120000, this, [this]() {
            if (m_validationProcess && m_validationProcess->state() != QProcess::NotRunning) {
                qWarning() << "[Orchestrator] Validation timeout";
                m_validationProcess->kill();
            }
        });
        break;
    }

    case OrchestratorCommand::Done:
        m_running = false;
        m_currentPhase = "completed";
        emit phaseChanged(m_currentPhase);
        qDebug() << "[Orchestrator] Completed:" << cmd.summary;
        cleanupInboxes();
        emit completed(cmd.summary);
        break;

    case OrchestratorCommand::Fail:
        m_running = false;
        m_currentPhase = "failed";
        emit phaseChanged(m_currentPhase);
        qDebug() << "[Orchestrator] Failed:" << cmd.reason;
        cleanupInboxes();
        emit failed(cmd.reason);
        break;

    default:
        feedResult("ERROR: Unknown action. Use delegate, validate, done, or fail.");
        break;
    }
}

// ─── Delegation Callback ─────────────────────────────────────────────────────

void Orchestrator::onDelegateFinished(const QString &childSessionId, const QString &output)
{
    if (!m_running) return;
    if (!m_pendingChildren.contains(childSessionId)) return;

    auto &pc = m_pendingChildren[childSessionId];
    pc.completed = true;
    pc.output = output;

    qDebug() << "[Orchestrator] Agent" << pc.agentName << "finished ("
             << pc.role << ")";

    checkAllChildrenDone();
}

void Orchestrator::checkAllChildrenDone()
{
    // Check if ALL pending children have completed
    for (const auto &pc : m_pendingChildren) {
        if (!pc.completed) return;
    }

    // All done — aggregate results
    QStringList parts;
    for (const auto &pc : m_pendingChildren) {
        parts << QStringLiteral("## Agent '%1' (%2) completed\n\n%3")
                     .arg(pc.agentName, pc.role, pc.output);
    }

    int count = m_pendingChildren.size();
    m_pendingChildren.clear();

    feedResult(QStringLiteral(
        "%1 agent(s) completed successfully.\n\n%2\n\n"
        "What should be done next?")
        .arg(count).arg(parts.join("\n\n---\n\n")));
}

// ─── Feed Result ─────────────────────────────────────────────────────────────

void Orchestrator::feedResult(const QString &resultText)
{
    if (!m_running || !m_chatPanel) return;

    m_mcpCommandReceived = false;  // reset for next turn

    emit progressUpdate(resultText.left(100) + "...");

    if (!m_orchestratorTurnDone) {
        // Claude CLI process is still running (common for fast validation commands).
        // Defer until onSessionFinishedProcessing picks it up.
        qDebug() << "[Orchestrator] Deferring feed result (process still running)";
        m_pendingFeedResult = resultText;
        return;
    }

    // Process is done, safe to send the next message
    m_orchestratorTurnDone = false;  // reset for the new turn
    m_pendingFeedResult.clear();
    m_chatPanel->sendMessageToSession(m_sessionId, resultText);
}
