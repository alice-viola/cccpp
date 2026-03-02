#include "core/DaemonClient.h"
#include "core/TelegramDaemon.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QThread>
#include <QFileInfo>
#include <QUuid>
#include <QDebug>

DaemonClient::DaemonClient(QObject *parent)
    : ChatHandler(parent)
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

bool DaemonClient::isConnected() const
{
    return m_socket->state() == QLocalSocket::ConnectedState;
}

bool DaemonClient::connectToDaemon()
{
    m_socket->connectToServer(TelegramDaemon::serverName());
    if (m_socket->waitForConnected(500))
        return true;

    if (m_socket->state() != QLocalSocket::UnconnectedState)
        m_socket->abort();

    if (!spawnDaemon()) return false;

    // Retry loop — connectToServer fails immediately if the server isn't listening yet,
    // so waitForConnected(3000) on a single attempt is useless. Poll until the daemon
    // socket appears (up to ~4s).
    for (int i = 0; i < 20; ++i) {
        QThread::msleep(200);
        m_socket->connectToServer(TelegramDaemon::serverName());
        if (m_socket->waitForConnected(200))
            return true;
        if (m_socket->state() != QLocalSocket::UnconnectedState)
            m_socket->abort();
    }

    qWarning() << "[DaemonClient] Failed to connect to daemon after spawn";
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
    setWorkingDirectory(workspace);
    m_workspaceName = name.isEmpty() ? QFileInfo(workspace).fileName() : name;

    QJsonObject msg;
    msg["type"] = "register";
    msg["workspace"] = m_workingDir;
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
        onChatMessage(chatId, text);
    } else if (type == "command") {
        qint64 chatId = static_cast<qint64>(msg["chat_id"].toDouble());
        QString cmd = msg["command"].toString();
        QString args = msg["args"].toString();
        onChatCommand(chatId, cmd, args);
    } else if (type == "callback_query") {
        qint64 chatId = static_cast<qint64>(msg["chat_id"].toDouble());
        QString queryId = msg["callback_query_id"].toString();
        QString data = msg["data"].toString();

        // Acknowledge callback via daemon
        QJsonObject answer;
        answer["type"] = "answer_callback";
        answer["callback_query_id"] = queryId;
        sendToDaemon(QJsonDocument(answer).toJson(QJsonDocument::Compact));

        onChatCallback(chatId, queryId, data);
    } else if (type == "message_id") {
        QString requestId = msg["request_id"].toString();
        qint64 messageId = static_cast<qint64>(msg["message_id"].toDouble());
        if (m_pendingMessageIds.contains(requestId)) {
            auto cb = m_pendingMessageIds.take(requestId);
            if (cb) cb(messageId);
        }
    }
}

// ---- Send primitives (via daemon IPC) ----

void DaemonClient::doSendResponse(qint64 chatId, const QString &text)
{
    QJsonObject msg;
    msg["type"] = "response";
    msg["chat_id"] = chatId;
    msg["text"] = text;
    sendToDaemon(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void DaemonClient::doSendEditResponse(qint64 chatId, qint64 msgId, const QString &text)
{
    QJsonObject msg;
    msg["type"] = "edit_response";
    msg["chat_id"] = chatId;
    msg["message_id"] = msgId;
    msg["text"] = text;
    sendToDaemon(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void DaemonClient::doSendWithKeyboard(qint64 chatId, const QString &text,
                                      const QJsonArray &rows)
{
    QJsonObject msg;
    msg["type"] = "send_keyboard";
    msg["chat_id"] = chatId;
    msg["text"] = text;
    msg["keyboard"] = rows;
    sendToDaemon(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void DaemonClient::doRequestSendMessage(qint64 chatId, const QString &text,
                                        std::function<void(qint64)> cb)
{
    QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (cb)
        m_pendingMessageIds[requestId] = cb;

    QJsonObject msg;
    msg["type"] = "send_message_with_id";
    msg["chat_id"] = chatId;
    msg["text"] = text;
    msg["request_id"] = requestId;
    sendToDaemon(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void DaemonClient::sendToDaemon(const QByteArray &data)
{
    if (!isConnected()) return;
    m_socket->write(data + '\n');
    m_socket->flush();
}
