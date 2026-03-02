#include "core/ChatHandler.h"
#include "core/ClaudeProcess.h"
#include "core/StreamParser.h"
#include "core/SessionManager.h"
#include "core/Database.h"
#include "core/GitManager.h"

#include <QJsonObject>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

using json = nlohmann::json;

static QString normalizePath(const QString &path)
{
    QString p = path;
    while (p.endsWith('/') && p.length() > 1)
        p.chop(1);
    return p;
}

static QJsonArray buildSessionKeyboardRows(const QList<SessionInfo> &sessions)
{
    QJsonArray rows;
    for (const auto &s : sessions) {
        QString title = s.title.isEmpty() ? s.sessionId.left(8) : s.title;
        if (title.length() > 22) title = title.left(19) + "...";

        QString dateStr;
        if (s.updatedAt > 0) {
            QDateTime dt = QDateTime::fromSecsSinceEpoch(s.updatedAt);
            dateStr = "  " + dt.toString("MMM d, HH:mm");
        }

        QJsonObject btn;
        btn["text"] = title + dateStr;
        btn["callback_data"] = "resume:" + s.sessionId;
        rows.append(QJsonArray{btn});
    }

    QJsonObject newBtn;
    newBtn["text"] = "+ New chat";
    newBtn["callback_data"] = "new";
    rows.append(QJsonArray{newBtn});
    return rows;
}

ChatHandler::ChatHandler(QObject *parent)
    : QObject(parent)
{
}

void ChatHandler::setSessionManager(SessionManager *mgr) { m_sessionMgr = mgr; }
void ChatHandler::setDatabase(Database *db) { m_database = db; }
void ChatHandler::setGitManager(GitManager *git) { m_gitManager = git; }

void ChatHandler::setWorkingDirectory(const QString &dir)
{
    m_workingDir = normalizePath(dir);
}

// ---- Entry points for subclasses ----

void ChatHandler::onChatMessage(qint64 chatId, const QString &text)
{
    auto &session = getOrCreateSession(chatId);
    QString trimmed = text.trimmed();

    if (trimmed.startsWith('/')) {
        int spaceIdx = trimmed.indexOf(' ');
        QString cmd = (spaceIdx > 0) ? trimmed.left(spaceIdx).toLower() : trimmed.toLower();
        QString args = (spaceIdx > 0) ? trimmed.mid(spaceIdx + 1).trimmed() : QString();
        // Strip leading '/' from cmd
        cmd = cmd.mid(1);
        onChatCommand(chatId, cmd, args);
    } else {
        handleFreeText(session, trimmed);
    }
}

void ChatHandler::onChatCommand(qint64 chatId, const QString &cmd, const QString &args)
{
    auto &session = getOrCreateSession(chatId);

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
    else if (cmd == "ws" || cmd == "workspaces" || cmd == "switch")
        doSendResponse(session.chatId, "Workspace: " + (m_workingDir.isEmpty() ? "(none)" : m_workingDir));
    else doSendResponse(session.chatId, "Unknown command. Send /help for available commands.");
}

void ChatHandler::onChatCallback(qint64 chatId, const QString &queryId, const QString &data)
{
    Q_UNUSED(queryId);
    auto &session = getOrCreateSession(chatId);

    if (data == "new")
        handleNew(session);
    else if (data.startsWith("resume:"))
        handleResume(session, data.mid(7));
}

// ---- Session management ----

ChatSession &ChatHandler::getOrCreateSession(qint64 chatId)
{
    if (!m_sessions.contains(chatId)) {
        ChatSession s;
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

void ChatHandler::createNewProcess(ChatSession &session)
{
    if (session.process) {
        session.process->cancel();
        session.process->deleteLater();
    }

    session.process = new ClaudeProcess(this);
    session.process->setWorkingDirectory(m_workingDir);
    qDebug() << "[ChatHandler] New process, workspace:" << m_workingDir;

    if (m_sessionMgr)
        session.sessionId = m_sessionMgr->createSession(m_workingDir);

    wireProcessSignals(session);
}

void ChatHandler::wireProcessSignals(ChatSession &session)
{
    auto *parser = session.process->streamParser();
    qint64 chatId = session.chatId;

    connect(session.process, &ClaudeProcess::started, this, [chatId] {
        qDebug() << "[ChatHandler] claude process started for chat" << chatId;
    });

    connect(parser, &StreamParser::textDelta, this, [this, chatId](const QString &text) {
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];
        s.accumulatedText += text;
        if (!s.flushTimer->isActive())
            s.flushTimer->start();
    });

    connect(parser, &StreamParser::toolUseStarted, this,
            [this, chatId](const QString &toolName, const QString &, const json &input) {
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

        if (m_database) {
            MessageRecord rec;
            rec.sessionId = s.sessionId;
            rec.role = "tool";
            rec.content = summary;
            rec.toolName = toolName;
            rec.toolInput = QString::fromStdString(input.dump());
            rec.turnId = s.turnId;
            rec.timestamp = QDateTime::currentSecsSinceEpoch();
            m_database->saveMessage(rec);
        }

        if (!s.flushTimer->isActive())
            s.flushTimer->start();
    });

    connect(parser, &StreamParser::resultReady, this,
            [this, chatId](const QString &sessionId, const json &) {
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];

        QString oldId = s.sessionId;
        if (!sessionId.isEmpty() && sessionId != s.sessionId) {
            if (m_database)
                m_database->deleteSession(oldId);
            if (m_sessionMgr) {
                m_sessionMgr->updateSessionId(s.sessionId, sessionId);
                if (m_database)
                    m_database->updateMessageSessionId(oldId, sessionId);
            }
            s.sessionId = sessionId;
            s.process->setSessionId(sessionId);
        }

        s.processing = false;
        s.flushTimer->stop();
        QString finalText = s.accumulatedText.trimmed();
        if (finalText.isEmpty() && !s.toolSummary.isEmpty()) {
            finalText = "Done.\n\nEdited:\n";
            for (const QString &t : s.toolSummary)
                finalText += "  " + t + "\n";
        }
        if (!finalText.isEmpty()) {
            sendFinalResponse(s, finalText);
            s.responseSent = true;
        }

        if (m_database && !finalText.isEmpty()) {
            MessageRecord rec;
            rec.sessionId = s.sessionId;
            rec.role = "assistant";
            rec.content = finalText;
            rec.turnId = s.turnId;
            rec.timestamp = QDateTime::currentSecsSinceEpoch();
            m_database->saveMessage(rec);
        }

        s.accumulatedText.clear();
        s.toolSummary.clear();
        s.statusMessageId = 0;
    });

    connect(session.process, &ClaudeProcess::errorOccurred, this,
            [this, chatId](const QString &error) {
        qDebug() << "[ChatHandler] process error for chat" << chatId << ":" << error;
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];
        s.processing = false;
        s.responseSent = true;
        s.accumulatedText.clear();
        s.toolSummary.clear();
        s.statusMessageId = 0;
        doSendResponse(chatId, "Error: " + error);
    });

    connect(session.process, &ClaudeProcess::finished, this,
            [this, chatId](int exitCode) {
        qDebug() << "[ChatHandler] process finished for chat" << chatId << "exit code:" << exitCode;
        if (!m_sessions.contains(chatId)) return;
        auto &s = m_sessions[chatId];
        s.flushTimer->stop();

        if (exitCode != 0) {
            s.processing = false;
            if (!s.responseSent)
                doSendResponse(chatId, QString("Error: Claude exited with code %1. The session may be invalid \u2014 try /new.").arg(exitCode));
        } else if (s.processing) {
            s.processing = false;
            QString text = s.accumulatedText.trimmed();
            if (!text.isEmpty())
                sendFinalResponse(s, text);
            else if (!s.responseSent)
                doSendResponse(chatId, "Done.");
        } else if (!s.responseSent) {
            doSendResponse(chatId, "Done.");
        }

        s.accumulatedText.clear();
        s.toolSummary.clear();
        s.statusMessageId = 0;
        s.responseSent = false;

        if (exitCode == 0)
            emit filesChanged();
    });
}

// ---- Response accumulation ----

void ChatHandler::flushAccumulated(qint64 chatId)
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
        doSendEditResponse(chatId, s.statusMessageId, display);
    } else {
        doRequestSendMessage(chatId, display, [this, chatId](qint64 msgId) {
            if (m_sessions.contains(chatId))
                m_sessions[chatId].statusMessageId = msgId;
        });
    }
}

void ChatHandler::sendFinalResponse(ChatSession &session, const QString &text)
{
    if (session.statusMessageId != 0) {
        doSendEditResponse(session.chatId, session.statusMessageId, text);
    } else {
        doSendResponse(session.chatId, text);
    }
}

// ---- Command handlers ----

void ChatHandler::handleFreeText(ChatSession &session, const QString &text)
{
    if (session.processing) {
        doSendResponse(session.chatId, "Still processing the previous message. Use /cancel to abort.");
        return;
    }

    if (!session.titleSet && !session.sessionId.isEmpty()) {
        session.titleSet = true;
        QString title = text.trimmed().left(50);
        if (m_sessionMgr)
            m_sessionMgr->setSessionTitle(session.sessionId, title);
    }

    qDebug() << "[ChatHandler] Sending to claude, workspace:" << m_workingDir
             << "session:" << session.sessionId
             << "text:" << text.left(80);

    session.turnId++;
    session.processing = true;
    session.responseSent = false;
    session.accumulatedText.clear();
    session.toolSummary.clear();
    session.statusMessageId = 0;

    if (m_database) {
        MessageRecord rec;
        rec.sessionId = session.sessionId;
        rec.role = "user";
        rec.content = text;
        rec.turnId = session.turnId;
        rec.timestamp = QDateTime::currentSecsSinceEpoch();
        m_database->saveMessage(rec);
    }

    session.process->sendMessage(text);
}

void ChatHandler::handleStatus(ChatSession &session)
{
    QString status;
    status += "Workspace: " + (m_workingDir.isEmpty() ? "(none)" : m_workingDir) + "\n";
    status += "Session: " + (session.sessionId.isEmpty() ? "(none)" : session.sessionId) + "\n";
    status += "Processing: " + QString(session.processing ? "yes" : "no") + "\n";
    if (m_gitManager && m_gitManager->isGitRepo())
        status += "Branch: " + m_gitManager->currentBranch();
    doSendResponse(session.chatId, status);
}

void ChatHandler::handleNew(ChatSession &session)
{
    if (session.processing) {
        doSendResponse(session.chatId, "Cannot create new session while processing. Use /cancel first.");
        return;
    }
    createNewProcess(session);
    session.titleSet = false;
    session.responseSent = false;
    doSendResponse(session.chatId, "New session started. Send a message to begin.");
}

void ChatHandler::handleSessions(ChatSession &session)
{
    QList<SessionInfo> sessions;

    if (m_database) {
        auto all = m_database->loadSessions();
        for (const auto &s : all) {
            if (normalizePath(s.workspace) == m_workingDir)
                sessions.append(s);
        }
        std::sort(sessions.begin(), sessions.end(), [](const SessionInfo &a, const SessionInfo &b) {
            return a.updatedAt > b.updatedAt;
        });
        if (sessions.size() > 15) sessions = sessions.mid(0, 15);
    } else if (m_sessionMgr) {
        sessions = m_sessionMgr->allSessions();
    }

    QString prompt = sessions.isEmpty()
        ? "No previous sessions. Start a new chat:"
        : "Choose a session to resume:";
    doSendWithKeyboard(session.chatId, prompt, buildSessionKeyboardRows(sessions));
}

void ChatHandler::handleResume(ChatSession &session, const QString &sessionId)
{
    if (session.processing) {
        doSendResponse(session.chatId, "Cannot switch while processing. Use /cancel first.");
        return;
    }
    if (session.process) {
        session.process->cancel();
        session.process->deleteLater();
    }
    session.process = new ClaudeProcess(this);
    session.process->setWorkingDirectory(m_workingDir);
    session.process->setSessionId(sessionId);
    session.sessionId = sessionId;
    session.titleSet = true;
    session.responseSent = false;
    wireProcessSignals(session);
    doSendResponse(session.chatId, "Session resumed. Send a message to continue.");
}

void ChatHandler::handleFiles(ChatSession &session)
{
    if (!m_gitManager) {
        doSendResponse(session.chatId, "Git manager not available.");
        return;
    }

    auto entries = m_gitManager->fileEntries();
    if (entries.isEmpty()) {
        doSendResponse(session.chatId, "No changed files.");
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
    doSendResponse(session.chatId, text);
}

void ChatHandler::handleDiff(ChatSession &session, const QString &args)
{
    if (!m_gitManager) {
        doSendResponse(session.chatId, "Git manager not available.");
        return;
    }

    if (args.isEmpty()) {
        QString output = m_gitManager->runGitSync({"diff", "--stat"});
        if (output.trimmed().isEmpty())
            output = "(No unstaged changes)";
        doSendResponse(session.chatId, output);
    } else {
        QString output = m_gitManager->runGitSync({"diff", args});
        if (output.trimmed().isEmpty())
            output = "(No changes for " + args + ")";
        if (output.length() > 3500)
            output = output.left(3500) + "\n... (truncated)";
        doSendResponse(session.chatId, "```\n" + output + "\n```");
    }
}

void ChatHandler::handleRevert(ChatSession &session)
{
    if (session.processing) {
        doSendResponse(session.chatId, "Cannot revert while processing. Use /cancel first.");
        return;
    }
    if (!m_database || session.sessionId.isEmpty()) {
        doSendResponse(session.chatId, "No session to revert.");
        return;
    }

    auto checkpoints = m_database->loadCheckpoints(session.sessionId);
    if (checkpoints.isEmpty()) {
        doSendResponse(session.chatId, "No checkpoints available for this session.");
        return;
    }

    auto &latest = checkpoints.last();
    doSendResponse(session.chatId, "Reverting to checkpoint at turn " + QString::number(latest.turnId) + "...");

    qint64 chatId = session.chatId;
    connect(session.process, &ClaudeProcess::rewindCompleted, this,
            [this, chatId](bool success) {
        if (success) doSendResponse(chatId, "Files reverted successfully.");
        else doSendResponse(chatId, "Revert failed.");
    }, Qt::SingleShotConnection);

    session.process->rewindFiles(latest.uuid);
}

void ChatHandler::handleCommit(ChatSession &session, const QString &args)
{
    if (!m_gitManager) {
        doSendResponse(session.chatId, "Git manager not available.");
        return;
    }
    if (args.isEmpty()) {
        doSendResponse(session.chatId, "Usage: /commit <message>");
        return;
    }

    qint64 chatId = session.chatId;
    connect(m_gitManager, &GitManager::commitSucceeded, this,
            [this, chatId](const QString &hash, const QString &msg) {
        doSendResponse(chatId, "Committed " + hash.left(7) + ": " + msg);
    }, Qt::SingleShotConnection);
    connect(m_gitManager, &GitManager::commitFailed, this,
            [this, chatId](const QString &err) {
        doSendResponse(chatId, "Commit failed: " + err);
    }, Qt::SingleShotConnection);

    m_gitManager->stageAll();
    m_gitManager->commit(args);
}

void ChatHandler::handleBranch(ChatSession &session)
{
    if (!m_gitManager) {
        doSendResponse(session.chatId, "Git manager not available.");
        return;
    }
    doSendResponse(session.chatId, "Branch: " + m_gitManager->currentBranch());
}

void ChatHandler::handleMode(ChatSession &session, const QString &args)
{
    if (args.isEmpty()) {
        doSendResponse(session.chatId, "Current mode: agent\nUsage: /mode agent|ask|plan");
        return;
    }

    QString mode = args.toLower();
    if (mode != "agent" && mode != "ask" && mode != "plan") {
        doSendResponse(session.chatId, "Invalid mode. Use: agent, ask, or plan");
        return;
    }

    if (session.process)
        session.process->setMode(mode);
    doSendResponse(session.chatId, "Mode set to: " + mode);
}

void ChatHandler::handleCancel(ChatSession &session)
{
    if (!session.processing) {
        doSendResponse(session.chatId, "Nothing to cancel.");
        return;
    }
    if (session.process)
        session.process->cancel();
    session.processing = false;
    session.responseSent = true;
    session.flushTimer->stop();
    session.accumulatedText.clear();
    session.toolSummary.clear();
    session.statusMessageId = 0;
    doSendResponse(session.chatId, "Cancelled.");
}

void ChatHandler::handleHelp(ChatSession &session)
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
    doSendResponse(session.chatId, help);
}
