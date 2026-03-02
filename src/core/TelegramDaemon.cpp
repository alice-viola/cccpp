#include "core/TelegramDaemon.h"
#include "core/TelegramApi.h"
#include "util/Config.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

#ifdef Q_OS_UNIX
#include <signal.h>
#endif

static const int kGracePeriodMs = 30000;

TelegramDaemon::TelegramDaemon(QObject *parent)
    : QObject(parent)
    , m_api(new TelegramApi(this))
    , m_server(new QLocalServer(this))
    , m_graceTimer(new QTimer(this))
{
    m_graceTimer->setSingleShot(true);
    m_graceTimer->setInterval(kGracePeriodMs);
    connect(m_graceTimer, &QTimer::timeout, this, &TelegramDaemon::checkGracePeriod);

    connect(m_server, &QLocalServer::newConnection,
            this, &TelegramDaemon::onNewConnection);
    connect(m_api, &TelegramApi::messageReceived,
            this, &TelegramDaemon::onTelegramMessage);
    connect(m_api, &TelegramApi::callbackQueryReceived,
            this, &TelegramDaemon::onTelegramCallback);
}

TelegramDaemon::~TelegramDaemon()
{
    stop();
}

QString TelegramDaemon::serverName()
{
    return QStringLiteral("cccpp-telegram");
}

QString TelegramDaemon::lockFilePath()
{
    return QDir::homePath() + "/.cccpp/telegram-daemon.lock";
}

bool TelegramDaemon::isDaemonRunning()
{
    // Try connecting to see if a daemon is alive
    QLocalSocket probe;
    probe.connectToServer(serverName());
    if (probe.waitForConnected(500)) {
        probe.disconnectFromServer();
        return true;
    }
    return false;
}

bool TelegramDaemon::removeStale()
{
    QString path = lockFilePath();
    if (!QFile::exists(path)) return false;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    qint64 pid = f.readAll().trimmed().toLongLong();
    f.close();

    // Check if process is alive
    if (pid > 0) {
#ifdef Q_OS_UNIX
        if (kill(static_cast<pid_t>(pid), 0) == 0)
            return false; // still running
#else
        // On Windows, could use OpenProcess — for now assume stale
#endif
    }

    // Stale lock — clean up
    QFile::remove(path);
    QLocalServer::removeServer(serverName());
    return true;
}

bool TelegramDaemon::start()
{
    auto &cfg = Config::instance();
    if (!cfg.telegramEnabled() || cfg.telegramBotToken().isEmpty()) {
        qWarning() << "[TelegramDaemon] Telegram not enabled or token empty";
        return false;
    }

    removeStale();

    if (!m_server->listen(serverName())) {
        qWarning() << "[TelegramDaemon] Cannot listen:" << m_server->errorString();
        return false;
    }

    m_api->setToken(cfg.telegramBotToken());
    m_api->setAllowedUsers(cfg.telegramAllowedUsers());

    writeLockFile();

    qDebug() << "[TelegramDaemon] Started, listening on" << serverName();

    // Start grace timer — if no instance connects within 30s, shut down
    m_graceTimer->start();

    return true;
}

void TelegramDaemon::stop()
{
    m_api->stopPolling();
    m_server->close();
    removeLockFile();
    qDebug() << "[TelegramDaemon] Stopped";
}

void TelegramDaemon::writeLockFile()
{
    QString path = lockFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArray::number(QCoreApplication::applicationPid()));
        f.close();
    }
}

void TelegramDaemon::removeLockFile()
{
    QFile::remove(lockFilePath());
}

// ---- Connection handling ----

void TelegramDaemon::onNewConnection()
{
    while (auto *socket = m_server->nextPendingConnection()) {
        m_graceTimer->stop(); // An instance connected, cancel grace timer

        if (!m_api->isPolling())
            m_api->startPolling();

        ConnectedInstance inst;
        inst.socket = socket;
        m_instances.append(inst);

        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            onInstanceData(socket);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            onInstanceDisconnected(socket);
        });

        qDebug() << "[TelegramDaemon] Instance connected, total:" << m_instances.size();
    }
}

void TelegramDaemon::onInstanceDisconnected(QLocalSocket *socket)
{
    for (int i = 0; i < m_instances.size(); ++i) {
        if (m_instances[i].socket == socket) {
            qDebug() << "[TelegramDaemon] Instance disconnected:" << m_instances[i].name;
            m_instances.removeAt(i);
            break;
        }
    }
    socket->deleteLater();

    if (m_instances.isEmpty()) {
        qDebug() << "[TelegramDaemon] No instances left, starting grace period";
        m_graceTimer->start();
    }
}

void TelegramDaemon::checkGracePeriod()
{
    if (m_instances.isEmpty()) {
        qDebug() << "[TelegramDaemon] Grace period expired, shutting down";
        stop();
        QCoreApplication::quit();
    }
}

void TelegramDaemon::onInstanceData(QLocalSocket *socket)
{
    // Find the instance
    ConnectedInstance *inst = nullptr;
    for (auto &i : m_instances) {
        if (i.socket == socket) { inst = &i; break; }
    }
    if (!inst) {
        qDebug() << "[TelegramDaemon] onInstanceData: socket NOT in m_instances!";
        return;
    }

    QByteArray newData = socket->readAll();
    qDebug() << "[TelegramDaemon] onInstanceData: received" << newData.size() << "bytes from" << inst->name;
    inst->readBuffer.append(newData);

    // Process newline-delimited JSON messages
    while (true) {
        int idx = inst->readBuffer.indexOf('\n');
        if (idx < 0) break;

        QByteArray line = inst->readBuffer.left(idx);
        inst->readBuffer.remove(0, idx + 1);

        if (line.isEmpty()) continue;

        QJsonObject msg = QJsonDocument::fromJson(line).object();
        QString type = msg["type"].toString();
        qDebug() << "[TelegramDaemon] processing IPC type:" << type << "(line size:" << line.size() << ")";

        if (type == "register") {
            inst->workspace = msg["workspace"].toString();
            inst->name = msg["name"].toString();
            qDebug() << "[TelegramDaemon] Registered:" << inst->name << inst->workspace;
        } else if (type == "unregister") {
            inst->workspace.clear();
            inst->name.clear();
        } else if (type == "response") {
            qint64 chatId = static_cast<qint64>(msg["chat_id"].toDouble());
            QString text = msg["text"].toString();
            m_api->sendMessage(chatId, text, {});
        } else if (type == "edit_response") {
            qint64 chatId = static_cast<qint64>(msg["chat_id"].toDouble());
            qint64 messageId = static_cast<qint64>(msg["message_id"].toDouble());
            QString text = msg["text"].toString();
            m_api->editMessage(chatId, messageId, text, {});
        } else if (type == "send_photo") {
            qint64 chatId = static_cast<qint64>(msg["chat_id"].toDouble());
            QByteArray imageData = QByteArray::fromBase64(msg["image_base64"].toString().toLatin1());
            QString caption = msg["caption"].toString();
            m_api->sendPhoto(chatId, imageData, caption);
        } else if (type == "send_keyboard") {
            qint64 chatId = static_cast<qint64>(msg["chat_id"].toDouble());
            QString text = msg["text"].toString();
            QJsonObject markup;
            markup["inline_keyboard"] = msg["keyboard"].toArray();
            qDebug() << "[TelegramDaemon] send_keyboard to chatId:" << chatId << "text:" << text << "markup:" << QJsonDocument(markup).toJson(QJsonDocument::Compact);
            m_api->sendMessage(chatId, text, {}, nullptr, markup);
        } else if (type == "answer_callback") {
            QString queryId = msg["callback_query_id"].toString();
            m_api->answerCallbackQuery(queryId);
        } else if (type == "send_message_with_id") {
            // Instance wants to send a message and get the message_id back
            qint64 chatId = static_cast<qint64>(msg["chat_id"].toDouble());
            QString text = msg["text"].toString();
            QString requestId = msg["request_id"].toString();
            QLocalSocket *sock = inst->socket;
            m_api->sendMessage(chatId, text, {}, [this, sock, requestId](qint64 msgId) {
                QJsonObject reply;
                reply["type"] = "message_id";
                reply["request_id"] = requestId;
                reply["message_id"] = msgId;
                sendToSocket(sock, QJsonDocument(reply).toJson(QJsonDocument::Compact));
            });
        }
    }
}

// ---- Telegram message routing ----

void TelegramDaemon::onTelegramMessage(const TelegramMessage &msg)
{
    QString text = msg.text.trimmed();

    // Daemon-level commands
    if (text.toLower() == "/ws" || text.toLower() == "/workspaces") {
        handleWsCommand(msg.chatId);
        return;
    }
    if (text.toLower().startsWith("/switch ")) {
        handleSwitchCommand(msg.chatId, text.mid(8).trimmed());
        return;
    }

    // Find target instance for this user
    auto it = m_userStates.find(msg.userId);
    QString targetWorkspace;

    if (it != m_userStates.end() && !it->activeWorkspace.isEmpty()) {
        targetWorkspace = it->activeWorkspace;
    } else if (m_instances.size() == 1) {
        // Auto-select the only instance
        targetWorkspace = m_instances.first().workspace;
        m_userStates[msg.userId] = {msg.chatId, targetWorkspace};
    } else if (m_instances.isEmpty()) {
        m_api->sendMessage(msg.chatId, "No CCCPP instances connected. Launch CCCPP first.", {});
        return;
    } else {
        // Multiple instances, user hasn't chosen
        handleWsCommand(msg.chatId);
        return;
    }

    // Route to target instance
    ConnectedInstance *inst = findInstance(targetWorkspace);
    if (!inst) {
        m_api->sendMessage(msg.chatId, "Workspace no longer connected. Use /ws to select another.", {});
        m_userStates[msg.userId].activeWorkspace.clear();
        return;
    }

    // Determine if it's a command or free text
    if (text.startsWith('/')) {
        int spaceIdx = text.indexOf(' ');
        QString cmd = (spaceIdx > 0) ? text.left(spaceIdx).mid(1).toLower() : text.mid(1).toLower();
        QString args = (spaceIdx > 0) ? text.mid(spaceIdx + 1).trimmed() : QString();

        QJsonObject ipcMsg;
        ipcMsg["type"] = "command";
        ipcMsg["chat_id"] = msg.chatId;
        ipcMsg["command"] = cmd;
        if (!args.isEmpty())
            ipcMsg["args"] = args;
        sendToSocket(inst->socket, QJsonDocument(ipcMsg).toJson(QJsonDocument::Compact));
    } else {
        QJsonObject ipcMsg;
        ipcMsg["type"] = "message";
        ipcMsg["chat_id"] = msg.chatId;
        ipcMsg["text"] = text;
        sendToSocket(inst->socket, QJsonDocument(ipcMsg).toJson(QJsonDocument::Compact));
    }
}

void TelegramDaemon::onTelegramCallback(const TelegramCallback &cb)
{
    auto it = m_userStates.find(cb.userId);
    if (it == m_userStates.end() || it->activeWorkspace.isEmpty()) {
        m_api->answerCallbackQuery(cb.callbackQueryId, "No active workspace");
        return;
    }

    ConnectedInstance *inst = findInstance(it->activeWorkspace);
    if (!inst) {
        m_api->answerCallbackQuery(cb.callbackQueryId, "Workspace not connected");
        m_userStates[cb.userId].activeWorkspace.clear();
        return;
    }

    QJsonObject ipcMsg;
    ipcMsg["type"] = "callback_query";
    ipcMsg["chat_id"] = cb.chatId;
    ipcMsg["callback_query_id"] = cb.callbackQueryId;
    ipcMsg["data"] = cb.data;
    sendToSocket(inst->socket, QJsonDocument(ipcMsg).toJson(QJsonDocument::Compact));
}

void TelegramDaemon::handleWsCommand(qint64 chatId)
{
    if (m_instances.isEmpty()) {
        m_api->sendMessage(chatId, "No CCCPP instances connected.", {});
        return;
    }

    QString text = "Connected workspaces:\n";
    for (int i = 0; i < m_instances.size(); ++i) {
        auto &inst = m_instances[i];
        QString label = inst.name.isEmpty() ? QFileInfo(inst.workspace).fileName() : inst.name;
        text += QString("  %1. %2\n     %3\n").arg(i + 1).arg(label, inst.workspace);
    }
    text += "\nUse /switch <number or name> to select.";
    m_api->sendMessage(chatId, text, {});
}

void TelegramDaemon::handleSwitchCommand(qint64 chatId, const QString &target)
{
    // Try number first
    bool ok;
    int idx = target.toInt(&ok) - 1;
    if (ok && idx >= 0 && idx < m_instances.size()) {
        auto &inst = m_instances[idx];
        // Find userId for this chatId (best-effort: iterate states)
        qint64 userId = 0;
        for (auto it = m_userStates.begin(); it != m_userStates.end(); ++it) {
            if (it->chatId == chatId) { userId = it.key(); break; }
        }
        if (userId == 0) {
            m_api->sendMessage(chatId, "Could not identify your user. Please send any message first.", {});
            return;
        }
        m_userStates[userId] = {chatId, inst.workspace};
        QString label = inst.name.isEmpty() ? QFileInfo(inst.workspace).fileName() : inst.name;
        m_api->sendMessage(chatId, "Switched to: " + label, {});
        return;
    }

    // Try name match
    for (auto &inst : m_instances) {
        QString label = inst.name.isEmpty() ? QFileInfo(inst.workspace).fileName() : inst.name;
        if (label.compare(target, Qt::CaseInsensitive) == 0 ||
            inst.workspace.endsWith("/" + target, Qt::CaseInsensitive)) {
            qint64 userId = 0;
            for (auto it = m_userStates.begin(); it != m_userStates.end(); ++it) {
                if (it->chatId == chatId) { userId = it.key(); break; }
            }
            if (userId == 0) userId = chatId;
            m_userStates[userId] = {chatId, inst.workspace};
            m_api->sendMessage(chatId, "Switched to: " + label, {});
            return;
        }
    }

    m_api->sendMessage(chatId, "Workspace not found: " + target + "\nUse /ws to list.", {});
}

// ---- Helpers ----

ConnectedInstance *TelegramDaemon::findInstance(const QString &workspace)
{
    for (auto &inst : m_instances) {
        if (inst.workspace == workspace)
            return &inst;
    }
    return nullptr;
}

void TelegramDaemon::sendToSocket(QLocalSocket *socket, const QByteArray &data)
{
    if (!socket || socket->state() != QLocalSocket::ConnectedState) return;
    socket->write(data + '\n');
    socket->flush();
}

