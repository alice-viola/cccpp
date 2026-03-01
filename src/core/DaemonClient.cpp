#include "core/DaemonClient.h"
#include "core/TelegramDaemon.h"
#include "core/ClaudeProcess.h"
#include "core/StreamParser.h"
#include "core/SessionManager.h"
#include "core/Database.h"
#include "core/GitManager.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QThread>
#include <QFileInfo>
#include <QUuid>
#include <QDebug>

DaemonClient::DaemonClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
{
    connect(m_socket, &QLocalSocket::connected, this, &DaemonClient::onConnected);
    connect(m_socket, &QLocalSocket::disconnected, this, &DaemonClient::onDisconnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &DaemonClient::onReadyRead);
}

DaemonClient::~DaemonClient()
{
    unregisterWorkspace();
}

void DaemonClient::setSessionManager(SessionManager *mgr) { m_sessionMgr = mgr; }
void DaemonClient::setDatabase(Database *db) { m_database = db; }
void DaemonClient::setGitManager(GitManager *git) { m_gitManager = git; }

void DaemonClient::setWorkingDirectory(const QString &dir)
{
    m_workingDir = dir;
    // Update existing sessions
    for (auto &s : m_sessions) {
        if (s.process)
            s.process->setWorkingDirectory(dir);
    }
}

bool DaemonClient::isConnected() const
{
    return m_socket->state() == QLocalSocket::ConnectedState;
}

bool DaemonClient::connectToDaemon()
{
    m_socket->connectToServer(TelegramDaemon::serverName());
    if (m_socket->waitForConnected(1000))
        return true;

    // Daemon not running — try to spawn it
    if (!spawnDaemon()) return false;

    // Retry connection with backoff
    for (int attempt = 0; attempt < 10; ++attempt) {
        QThread::msleep(300);
        m_socket->connectToServer(TelegramDaemon::serverName());
        if (m_socket->waitForConnected(500))
            return true;
    }

    qWarning() << "[DaemonClient] Failed to connect to daemon";
    emit connectionFailed();
    return false;
}

bool DaemonClient::spawnDaemon()
{
    QString appPath = QCoreApplication::applicationFilePath();
    bool started = QProcess::startDetached(appPath, {"--daemon"});
    if (!started)
        qWarning() << "[DaemonClient] Failed to spawn daemon";
    return started;
}

void DaemonClient::registerWorkspace(const QString &workspace, const QString &name)
{
    m_workingDir = workspace;
    m_workspaceName = name.isEmpty() ? QFileInfo(workspace).fileName() : name;

    QJsonObject msg;
    msg["type"] = "register";
    msg["workspace"] = workspace;
    msg["name"] = m_workspaceName;
    sendToDaemon(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void DaemonClient::unregisterWorkspace()
{
    if (!isConnected()) return;
    QJsonObject msg;
    msg["type"] = "unregister";
    msg["workspace"] = m_workingDir;
    sendToDaemon(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

// ---- Connection events ----

void DaemonClient::onConnected()
{
    qDebug() << "[DaemonClient] Connected to daemon";
    if (!m_workingDir.isEmpty())
        registerWorkspace(m_workingDir, m_workspaceName);
    emit connected();
}

void DaemonClient::onDisconnected()
{
    qDebug() << "[DaemonClient] Disconnected from daemon";
    emit disconnected();
}

void DaemonClient::onReadyRead()
{
    m_readBuffer.append(m_socket->readAll());

    while (true) {
        int idx = m_readBuffer.indexOf('\n');
        if (idx < 0) break;

        QByteArray line = m_readBuffer.left(idx);
        m_readBuffer.remove(0, idx + 1);

        if (!line.isEmpty())
            processMessage(line);
    }
}

void DaemonClient::processMessage(const QByteArray &line)
{
    QJsonObject msg = QJsonDocument::fromJson(line).object();
    QString type = msg["type"].toString();

    if (type == "message") {
        qint64 chatId = static_cast<qint64>(msg["chat_id"].toDouble());
        QString text = msg["text"].toString();
        auto &session = getOrCreateSession(chatId);
        handleFreeText(session, text);
    } else if (type == "command") {
        qint64 chatId = static_cast<qint64>(msg["chat_id"].toDouble());
        QString cmd = msg["command"].toString();
        QString args = msg["args"].toString();
        auto &session = getOrCreateSession(chatId);
        handleCommand(session, cmd, args);
    } else if (type == "message_id") {
        // Response to send_message_with_id request
        QString requestId = msg["request_id"].toString();
        qint64 messageId = static_cast<qint64>(msg["message_id"].toDouble());
        if (m_pendingMessageIds.contains(requestId)) {
            auto cb = m_pendingMessageIds.take(requestId);
            if (cb) cb(messageId);
        }
    }
}

// ---- Session management ----

DaemonChatSession &DaemonClient::getOrCreateSession(qint64 chatId)
{
    if (!m_sessions.contains(chatId)) {
        DaemonChatSession s;
        s.chatId = chatId;

        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(kFlushIntervalMs);
        connect(timer, &QTimer::timeout, this, [this, chatId] {
            flushAccumulated(chatId);
        });
        s.flushTimer = timer;

        m_sessions[chatId] = s;
        createNewProcess(m_sessions[chatId]);
    }
    return m_sessions[chatId];
}

void DaemonClient::createNewProcess(DaemonChatSession &session)
{
    if (session.process) {
        session.process->cancel();
        session.process->deleteLater();
    }

    session.process = new ClaudeProcess(this);
    session.process->setWorkingDirectory(m_workingDir);

    if (m_sessionMgr) {
        session.sessionId = m_sessionMgr->createSession(m_workingDir);
        session.process->setSessionId(session.sessionId);
    }

    wireProcessSignals(session);
}

void DaemonClient::wireProcessSignals(DaemonChatSession &session)
{
    auto *parser = session.process->streamParser();
    qint64 chatId = session.chatId;

    connect(parser, &StreamParser::textDelta, this, [this, chatId](const QString &text) {
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];
        s.accumulatedText += text;
        if (!s.flushTimer->isActive())
            s.flushTimer->start();
    });

    connect(parser, &StreamParser::toolUseStarted, this,
            [this, chatId](const QString &toolName, const json &input) {
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];

        QString summary = toolName;
        if (input.contains("file_path"))
            summary += ": " + QString::fromStdString(input["file_path"].get<std::string>());
        else if (input.contains("command")) {
            auto cmd = QString::fromStdString(input["command"].get<std::string>());
            summary += ": " + cmd.left(60);
        }
        s.toolSummary.append(summary);

        if (!s.flushTimer->isActive())
            s.flushTimer->start();
    });

    connect(parser, &StreamParser::resultReady, this,
            [this, chatId](const QString &sessionId, const json &) {
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];

        if (!sessionId.isEmpty() && sessionId != s.sessionId) {
            if (m_sessionMgr)
                m_sessionMgr->updateSessionId(s.sessionId, sessionId);
            s.sessionId = sessionId;
            s.process->setSessionId(sessionId);
        }

        s.processing = false;
        s.flushTimer->stop();
        QString finalText = s.accumulatedText.trimmed();
        if (finalText.isEmpty()) finalText = "(No text response)";
        sendFinalResponse(s, finalText);

        s.accumulatedText.clear();
        s.toolSummary.clear();
        s.statusMessageId = 0;
    });

    connect(session.process, &ClaudeProcess::errorOccurred, this,
            [this, chatId](const QString &error) {
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];
        s.processing = false;
        s.accumulatedText.clear();
        s.toolSummary.clear();
        s.statusMessageId = 0;
        sendResponse(chatId, "Error: " + error);
    });

    connect(session.process, &ClaudeProcess::finished, this,
            [this, chatId](int exitCode) {
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];
        if (s.processing) {
            s.processing = false;
            s.flushTimer->stop();
            QString text = s.accumulatedText.trimmed();
            if (!text.isEmpty())
                sendFinalResponse(s, text);
            else if (exitCode != 0)
                sendResponse(chatId, QString("Process exited with code %1").arg(exitCode));
            s.accumulatedText.clear();
            s.toolSummary.clear();
            s.statusMessageId = 0;
        }
    });
}

// ---- Response accumulation (via daemon IPC) ----

void DaemonClient::flushAccumulated(qint64 chatId)
{
    if (!m_sessions.contains(chatId)) return;
    auto &s = m_sessions[chatId];

    QString text = s.accumulatedText.trimmed();
    if (text.isEmpty() && s.toolSummary.isEmpty()) return;

    QString display;
    if (!text.isEmpty()) {
        display = text.left(3000);
        if (text.length() > 3000) display += "\n...";
    }
    if (!s.toolSummary.isEmpty()) {
        if (!display.isEmpty()) display += "\n\n";
        display += "Tools:\n";
        for (const QString &t : s.toolSummary)
            display += "  " + t + "\n";
    }

    if (s.statusMessageId != 0) {
        sendEditResponse(chatId, s.statusMessageId, display);
    } else {
        requestSendMessage(chatId, display, [this, chatId](qint64 msgId) {
            if (m_sessions.contains(chatId))
                m_sessions[chatId].statusMessageId = msgId;
        });
    }
}

void DaemonClient::sendFinalResponse(DaemonChatSession &session, const QString &text)
{
    if (session.statusMessageId != 0) {
        sendEditResponse(session.chatId, session.statusMessageId, text);
    } else {
        sendResponse(session.chatId, text);
    }
}

// ---- Command handlers ----

void DaemonClient::handleCommand(DaemonChatSession &session, const QString &cmd, const QString &args)
{
    if      (cmd == "status")   handleStatus(session);
    else if (cmd == "new")      handleNew(session);
    else if (cmd == "sessions") handleSessions(session);
    else if (cmd == "files")    handleFiles(session);
    else if (cmd == "diff")     handleDiff(session, args);
    else if (cmd == "revert")   handleRevert(session);
    else if (cmd == "commit")   handleCommit(session, args);
    else if (cmd == "branch")   handleBranch(session);
    else if (cmd == "mode")     handleMode(session, args);
    else if (cmd == "cancel")   handleCancel(session);
    else if (cmd == "help" || cmd == "start") handleHelp(session);
    else sendResponse(session.chatId, "Unknown command. Send /help for available commands.");
}

void DaemonClient::handleFreeText(DaemonChatSession &session, const QString &text)
{
    if (session.processing) {
        sendResponse(session.chatId, "Still processing the previous message. Use /cancel to abort.");
        return;
    }
    session.processing = true;
    session.accumulatedText.clear();
    session.toolSummary.clear();
    session.statusMessageId = 0;
    session.process->sendMessage(text);
}

void DaemonClient::handleStatus(DaemonChatSession &session)
{
    QString status;
    status += "Workspace: " + (m_workingDir.isEmpty() ? "(none)" : m_workingDir) + "\n";
    status += "Session: " + (session.sessionId.isEmpty() ? "(none)" : session.sessionId) + "\n";
    status += "Processing: " + QString(session.processing ? "yes" : "no") + "\n";
    if (m_gitManager && m_gitManager->isGitRepo())
        status += "Branch: " + m_gitManager->currentBranch();
    sendResponse(session.chatId, status);
}

void DaemonClient::handleNew(DaemonChatSession &session)
{
    if (session.processing) {
        sendResponse(session.chatId, "Cannot create new session while processing. Use /cancel first.");
        return;
    }
    createNewProcess(session);
    sendResponse(session.chatId, "New session created: " + session.sessionId);
}

void DaemonClient::handleSessions(DaemonChatSession &session)
{
    if (!m_sessionMgr) {
        sendResponse(session.chatId, "Session manager not available.");
        return;
    }
    auto sessions = m_sessionMgr->allSessions();
    if (sessions.isEmpty()) {
        sendResponse(session.chatId, "No sessions.");
        return;
    }
    QString text = "Sessions:\n";
    for (const auto &s : sessions) {
        text += QString("  %1 - %2 [%3]\n")
            .arg(s.sessionId.left(8), s.title.isEmpty() ? "(untitled)" : s.title, s.mode);
    }
    sendResponse(session.chatId, text);
}

void DaemonClient::handleFiles(DaemonChatSession &session)
{
    if (!m_gitManager) {
        sendResponse(session.chatId, "Git manager not available.");
        return;
    }
    auto entries = m_gitManager->fileEntries();
    if (entries.isEmpty()) {
        sendResponse(session.chatId, "No changed files.");
        return;
    }
    QString text = "Changed files:\n";
    for (const auto &e : entries) {
        QChar statusChar = '?';
        auto effectiveStatus = (e.indexStatus != GitFileStatus::Unmodified)
            ? e.indexStatus : e.workStatus;
        switch (effectiveStatus) {
            case GitFileStatus::Modified:  statusChar = 'M'; break;
            case GitFileStatus::Added:     statusChar = 'A'; break;
            case GitFileStatus::Deleted:   statusChar = 'D'; break;
            case GitFileStatus::Renamed:   statusChar = 'R'; break;
            case GitFileStatus::Untracked: statusChar = '?'; break;
            default: statusChar = ' '; break;
        }
        text += QString("  %1 %2\n").arg(statusChar).arg(e.filePath);
    }
    sendResponse(session.chatId, text);
}

void DaemonClient::handleDiff(DaemonChatSession &session, const QString &args)
{
    if (!m_gitManager) {
        sendResponse(session.chatId, "Git manager not available.");
        return;
    }
    if (args.isEmpty()) {
        QString output = m_gitManager->runGitSync({"diff", "--stat"});
        if (output.trimmed().isEmpty()) output = "(No unstaged changes)";
        sendResponse(session.chatId, output);
    } else {
        QString output = m_gitManager->runGitSync({"diff", args});
        if (output.trimmed().isEmpty()) output = "(No changes for " + args + ")";
        if (output.length() > 3500) output = output.left(3500) + "\n... (truncated)";
        sendResponse(session.chatId, "```\n" + output + "\n```");
    }
}

void DaemonClient::handleRevert(DaemonChatSession &session)
{
    if (session.processing) {
        sendResponse(session.chatId, "Cannot revert while processing. Use /cancel first.");
        return;
    }
    if (!m_database || session.sessionId.isEmpty()) {
        sendResponse(session.chatId, "No session to revert.");
        return;
    }
    auto checkpoints = m_database->loadCheckpoints(session.sessionId);
    if (checkpoints.isEmpty()) {
        sendResponse(session.chatId, "No checkpoints available for this session.");
        return;
    }
    auto &latest = checkpoints.last();
    sendResponse(session.chatId, "Reverting to checkpoint at turn " + QString::number(latest.turnId) + "...");

    qint64 chatId = session.chatId;
    connect(session.process, &ClaudeProcess::rewindCompleted, this,
            [this, chatId](bool success) {
        if (success) sendResponse(chatId, "Files reverted successfully.");
        else sendResponse(chatId, "Revert failed.");
    }, Qt::SingleShotConnection);

    session.process->rewindFiles(latest.uuid);
}

void DaemonClient::handleCommit(DaemonChatSession &session, const QString &args)
{
    if (!m_gitManager) {
        sendResponse(session.chatId, "Git manager not available.");
        return;
    }
    if (args.isEmpty()) {
        sendResponse(session.chatId, "Usage: /commit <message>");
        return;
    }
    qint64 chatId = session.chatId;
    connect(m_gitManager, &GitManager::commitSucceeded, this,
            [this, chatId](const QString &hash, const QString &msg) {
        sendResponse(chatId, "Committed " + hash.left(7) + ": " + msg);
    }, Qt::SingleShotConnection);
    connect(m_gitManager, &GitManager::commitFailed, this,
            [this, chatId](const QString &err) {
        sendResponse(chatId, "Commit failed: " + err);
    }, Qt::SingleShotConnection);
    m_gitManager->stageAll();
    m_gitManager->commit(args);
}

void DaemonClient::handleBranch(DaemonChatSession &session)
{
    if (!m_gitManager) {
        sendResponse(session.chatId, "Git manager not available.");
        return;
    }
    sendResponse(session.chatId, "Branch: " + m_gitManager->currentBranch());
}

void DaemonClient::handleMode(DaemonChatSession &session, const QString &args)
{
    if (args.isEmpty()) {
        sendResponse(session.chatId, "Current mode: agent\nUsage: /mode agent|ask|plan");
        return;
    }
    QString mode = args.toLower();
    if (mode != "agent" && mode != "ask" && mode != "plan") {
        sendResponse(session.chatId, "Invalid mode. Use: agent, ask, or plan");
        return;
    }
    if (session.process) session.process->setMode(mode);
    sendResponse(session.chatId, "Mode set to: " + mode);
}

void DaemonClient::handleCancel(DaemonChatSession &session)
{
    if (!session.processing) {
        sendResponse(session.chatId, "Nothing to cancel.");
        return;
    }
    if (session.process) session.process->cancel();
    session.processing = false;
    session.flushTimer->stop();
    session.accumulatedText.clear();
    session.toolSummary.clear();
    session.statusMessageId = 0;
    sendResponse(session.chatId, "Cancelled.");
}

void DaemonClient::handleHelp(DaemonChatSession &session)
{
    QString help =
        "CCCPP Telegram Bot\n\n"
        "Send any text to chat with Claude.\n\n"
        "Commands:\n"
        "/status - Current session info\n"
        "/new - Start a new session\n"
        "/sessions - List all sessions\n"
        "/files - List changed files\n"
        "/diff [file] - Show git diff\n"
        "/revert - Revert to last checkpoint\n"
        "/commit <msg> - Stage all & commit\n"
        "/branch - Current git branch\n"
        "/mode <agent|ask|plan> - Set mode\n"
        "/cancel - Cancel current request\n"
        "/ws - List connected workspaces\n"
        "/switch <n> - Switch workspace\n"
        "/help - This message";
    sendResponse(session.chatId, help);
}

// ---- IPC helpers ----

void DaemonClient::sendToDaemon(const QByteArray &data)
{
    if (!isConnected()) return;
    m_socket->write(data + '\n');
    m_socket->flush();
}

void DaemonClient::sendResponse(qint64 chatId, const QString &text)
{
    QJsonObject msg;
    msg["type"] = "response";
    msg["chat_id"] = chatId;
    msg["text"] = text;
    sendToDaemon(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void DaemonClient::sendEditResponse(qint64 chatId, qint64 messageId, const QString &text)
{
    QJsonObject msg;
    msg["type"] = "edit_response";
    msg["chat_id"] = chatId;
    msg["message_id"] = messageId;
    msg["text"] = text;
    sendToDaemon(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void DaemonClient::requestSendMessage(qint64 chatId, const QString &text,
                                      std::function<void(qint64)> callback)
{
    QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (callback)
        m_pendingMessageIds[requestId] = callback;

    QJsonObject msg;
    msg["type"] = "send_message_with_id";
    msg["chat_id"] = chatId;
    msg["text"] = text;
    msg["request_id"] = requestId;
    sendToDaemon(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}
