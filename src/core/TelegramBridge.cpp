#include "core/TelegramBridge.h"
#include "core/TelegramApi.h"
#include "core/ClaudeProcess.h"
#include "core/StreamParser.h"
#include "core/SessionManager.h"
#include "core/Database.h"
#include "core/GitManager.h"

#include <QDebug>

static const int kFlushIntervalMs = 2000;

TelegramBridge::TelegramBridge(TelegramApi *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
    connect(m_api, &TelegramApi::messageReceived,
            this, &TelegramBridge::onMessage);
}

void TelegramBridge::setSessionManager(SessionManager *mgr) { m_sessionMgr = mgr; }
void TelegramBridge::setDatabase(Database *db) { m_database = db; }
void TelegramBridge::setGitManager(GitManager *git) { m_gitManager = git; }

void TelegramBridge::setWorkingDirectory(const QString &dir)
{
    m_workingDir = dir;
}

// ---- Message routing ----

void TelegramBridge::onMessage(const TelegramMessage &msg)
{
    auto &session = getOrCreateSession(msg.chatId);
    QString text = msg.text.trimmed();

    if (text.startsWith('/')) {
        int spaceIdx = text.indexOf(' ');
        QString cmd = (spaceIdx > 0) ? text.left(spaceIdx).toLower() : text.toLower();
        QString args = (spaceIdx > 0) ? text.mid(spaceIdx + 1).trimmed() : QString();

        if      (cmd == "/status")   handleStatus(session);
        else if (cmd == "/new")      handleNew(session);
        else if (cmd == "/sessions") handleSessions(session);
        else if (cmd == "/files")    handleFiles(session);
        else if (cmd == "/diff")     handleDiff(session, args);
        else if (cmd == "/revert")   handleRevert(session);
        else if (cmd == "/commit")   handleCommit(session, args);
        else if (cmd == "/branch")   handleBranch(session);
        else if (cmd == "/mode")     handleMode(session, args);
        else if (cmd == "/cancel")   handleCancel(session);
        else if (cmd == "/help" || cmd == "/start") handleHelp(session);
        else send(session.chatId, "Unknown command. Send /help for available commands.");
    } else {
        handleFreeText(session, text);
    }
}

// ---- Session management ----

TelegramSession &TelegramBridge::getOrCreateSession(qint64 chatId)
{
    if (!m_sessions.contains(chatId)) {
        TelegramSession s;
        s.chatId = chatId;
        s.workspace = m_workingDir;

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

void TelegramBridge::createNewProcess(TelegramSession &session)
{
    if (session.process) {
        session.process->cancel();
        session.process->deleteLater();
    }

    session.process = new ClaudeProcess(this);
    session.process->setWorkingDirectory(session.workspace);

    if (m_sessionMgr) {
        session.sessionId = m_sessionMgr->createSession(session.workspace);
        session.process->setSessionId(session.sessionId);
    }

    wireProcessSignals(session);
}

void TelegramBridge::wireProcessSignals(TelegramSession &session)
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

        // Update session ID if the CLI returned a real one
        if (!sessionId.isEmpty() && sessionId != s.sessionId) {
            if (m_sessionMgr)
                m_sessionMgr->updateSessionId(s.sessionId, sessionId);
            s.sessionId = sessionId;
            s.process->setSessionId(sessionId);
        }

        s.processing = false;

        // Send final accumulated text
        s.flushTimer->stop();
        QString finalText = s.accumulatedText.trimmed();
        if (finalText.isEmpty())
            finalText = "(No text response)";
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
        send(chatId, "Error: " + error);
    });

    connect(session.process, &ClaudeProcess::finished, this,
            [this, chatId](int exitCode) {
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];
        if (s.processing) {
            // Process ended unexpectedly
            s.processing = false;
            s.flushTimer->stop();
            QString text = s.accumulatedText.trimmed();
            if (!text.isEmpty())
                sendFinalResponse(s, text);
            else if (exitCode != 0)
                send(chatId, QString("Process exited with code %1").arg(exitCode));
            s.accumulatedText.clear();
            s.toolSummary.clear();
            s.statusMessageId = 0;
        }
    });
}

// ---- Response accumulation ----

void TelegramBridge::flushAccumulated(qint64 chatId)
{
    if (!m_sessions.contains(chatId)) return;
    auto &s = m_sessions[chatId];

    QString text = s.accumulatedText.trimmed();
    if (text.isEmpty() && s.toolSummary.isEmpty()) return;

    // Build status update text
    QString display;
    if (!text.isEmpty()) {
        // Truncate for in-progress updates (will send full on completion)
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
        // Edit the existing "Working..." message in-place
        m_api->editMessage(chatId, s.statusMessageId, display, {});
    } else {
        // Send initial status message
        m_api->sendMessage(chatId, display, {}, [this, chatId](qint64 msgId) {
            if (m_sessions.contains(chatId))
                m_sessions[chatId].statusMessageId = msgId;
        });
    }
}

void TelegramBridge::sendFinalResponse(TelegramSession &session, const QString &text)
{
    if (session.statusMessageId != 0) {
        // Edit in-place with final content
        m_api->editMessage(session.chatId, session.statusMessageId, text, {});
    } else {
        send(session.chatId, text);
    }
}

// ---- Command handlers ----

void TelegramBridge::handleFreeText(TelegramSession &session, const QString &text)
{
    if (session.processing) {
        send(session.chatId, "Still processing the previous message. Use /cancel to abort.");
        return;
    }

    session.processing = true;
    session.accumulatedText.clear();
    session.toolSummary.clear();
    session.statusMessageId = 0;

    session.process->sendMessage(text);
}

void TelegramBridge::handleStatus(TelegramSession &session)
{
    QString status;
    status += "Workspace: " + (session.workspace.isEmpty() ? "(none)" : session.workspace) + "\n";
    status += "Session: " + (session.sessionId.isEmpty() ? "(none)" : session.sessionId) + "\n";
    status += "Processing: " + QString(session.processing ? "yes" : "no") + "\n";
    if (m_gitManager && m_gitManager->isGitRepo())
        status += "Branch: " + m_gitManager->currentBranch();
    send(session.chatId, status);
}

void TelegramBridge::handleNew(TelegramSession &session)
{
    if (session.processing) {
        send(session.chatId, "Cannot create new session while processing. Use /cancel first.");
        return;
    }
    createNewProcess(session);
    send(session.chatId, "New session created: " + session.sessionId);
}

void TelegramBridge::handleSessions(TelegramSession &session)
{
    if (!m_sessionMgr) {
        send(session.chatId, "Session manager not available.");
        return;
    }

    auto sessions = m_sessionMgr->allSessions();
    if (sessions.isEmpty()) {
        send(session.chatId, "No sessions.");
        return;
    }

    QString text = "Sessions:\n";
    for (const auto &s : sessions) {
        text += QString("  %1 - %2 [%3]\n")
            .arg(s.sessionId.left(8), s.title.isEmpty() ? "(untitled)" : s.title, s.mode);
    }
    send(session.chatId, text);
}

void TelegramBridge::handleFiles(TelegramSession &session)
{
    if (!m_gitManager) {
        send(session.chatId, "Git manager not available.");
        return;
    }

    auto entries = m_gitManager->fileEntries();
    if (entries.isEmpty()) {
        send(session.chatId, "No changed files.");
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
    send(session.chatId, text);
}

void TelegramBridge::handleDiff(TelegramSession &session, const QString &args)
{
    if (!m_gitManager) {
        send(session.chatId, "Git manager not available.");
        return;
    }

    if (args.isEmpty()) {
        // Show summary diff
        QString output = m_gitManager->runGitSync({"diff", "--stat"});
        if (output.trimmed().isEmpty())
            output = "(No unstaged changes)";
        send(session.chatId, output);
    } else {
        // Show diff for specific file
        QString output = m_gitManager->runGitSync({"diff", args});
        if (output.trimmed().isEmpty())
            output = "(No changes for " + args + ")";
        // Truncate long diffs
        if (output.length() > 3500)
            output = output.left(3500) + "\n... (truncated)";
        send(session.chatId, "```\n" + output + "\n```");
    }
}

void TelegramBridge::handleRevert(TelegramSession &session)
{
    if (session.processing) {
        send(session.chatId, "Cannot revert while processing. Use /cancel first.");
        return;
    }
    if (!m_database || session.sessionId.isEmpty()) {
        send(session.chatId, "No session to revert.");
        return;
    }

    auto checkpoints = m_database->loadCheckpoints(session.sessionId);
    if (checkpoints.isEmpty()) {
        send(session.chatId, "No checkpoints available for this session.");
        return;
    }

    // Use the most recent checkpoint
    auto &latest = checkpoints.last();
    send(session.chatId, "Reverting to checkpoint at turn " + QString::number(latest.turnId) + "...");

    connect(session.process, &ClaudeProcess::rewindCompleted, this,
            [this, chatId = session.chatId](bool success) {
        if (success)
            send(chatId, "Files reverted successfully.");
        else
            send(chatId, "Revert failed.");
    }, Qt::SingleShotConnection);

    session.process->rewindFiles(latest.uuid);
}

void TelegramBridge::handleCommit(TelegramSession &session, const QString &args)
{
    if (!m_gitManager) {
        send(session.chatId, "Git manager not available.");
        return;
    }
    if (args.isEmpty()) {
        send(session.chatId, "Usage: /commit <message>");
        return;
    }

    qint64 chatId = session.chatId;

    // Connect one-shot signals for commit result
    connect(m_gitManager, &GitManager::commitSucceeded, this,
            [this, chatId](const QString &hash, const QString &msg) {
        send(chatId, "Committed " + hash.left(7) + ": " + msg);
    }, Qt::SingleShotConnection);

    connect(m_gitManager, &GitManager::commitFailed, this,
            [this, chatId](const QString &err) {
        send(chatId, "Commit failed: " + err);
    }, Qt::SingleShotConnection);

    m_gitManager->stageAll();
    m_gitManager->commit(args);
}

void TelegramBridge::handleBranch(TelegramSession &session)
{
    if (!m_gitManager) {
        send(session.chatId, "Git manager not available.");
        return;
    }
    send(session.chatId, "Branch: " + m_gitManager->currentBranch());
}

void TelegramBridge::handleMode(TelegramSession &session, const QString &args)
{
    if (args.isEmpty()) {
        send(session.chatId, "Current mode: agent\nUsage: /mode agent|ask|plan");
        return;
    }

    QString mode = args.toLower();
    if (mode != "agent" && mode != "ask" && mode != "plan") {
        send(session.chatId, "Invalid mode. Use: agent, ask, or plan");
        return;
    }

    if (session.process)
        session.process->setMode(mode);
    send(session.chatId, "Mode set to: " + mode);
}

void TelegramBridge::handleCancel(TelegramSession &session)
{
    if (!session.processing) {
        send(session.chatId, "Nothing to cancel.");
        return;
    }
    if (session.process)
        session.process->cancel();
    session.processing = false;
    session.flushTimer->stop();
    session.accumulatedText.clear();
    session.toolSummary.clear();
    session.statusMessageId = 0;
    send(session.chatId, "Cancelled.");
}

void TelegramBridge::handleHelp(TelegramSession &session)
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
        "/help - This message";
    send(session.chatId, help);
}

// ---- Helpers ----

void TelegramBridge::send(qint64 chatId, const QString &text)
{
    m_api->sendMessage(chatId, text, {});
}

QString TelegramBridge::escapeMarkdown(const QString &text)
{
    QString escaped = text;
    for (QChar c : {'_', '*', '[', ']', '(', ')', '~', '`', '>', '#', '+', '-', '=', '|', '{', '}', '.', '!'}) {
        escaped.replace(c, "\\" + QString(c));
    }
    return escaped;
}
