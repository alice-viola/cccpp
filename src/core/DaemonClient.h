#pragma once

#include "core/ChatHandler.h"
#include <QLocalSocket>
#include <QTimer>

class DaemonClient : public ChatHandler {
    Q_OBJECT
public:
    explicit DaemonClient(QObject *parent = nullptr);
    ~DaemonClient();

    bool connectToDaemon();
    void registerWorkspace(const QString &workspace, const QString &name = {});
    void unregisterWorkspace();

    bool isConnected() const;

    static bool spawnDaemon();

signals:
    void connected();
    void disconnected();
    void connectionFailed();

protected:
    void doSendResponse(qint64 chatId, const QString &text) override;
    void doSendEditResponse(qint64 chatId, qint64 msgId, const QString &text) override;
    void doSendWithKeyboard(qint64 chatId, const QString &text,
                            const QJsonArray &rows) override;
    void doRequestSendMessage(qint64 chatId, const QString &text,
                              std::function<void(qint64)> cb) override;

private:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void processMessage(const QByteArray &line);
    void sendToDaemon(const QByteArray &data);
    void attemptReconnect();

    QLocalSocket *m_socket = nullptr;
    QByteArray m_readBuffer;
    QString m_workspaceName;
    QMap<QString, std::function<void(qint64)>> m_pendingMessageIds;

    QTimer *m_reconnectTimer = nullptr;
    int m_reconnectAttempts = 0;
    static constexpr int kMaxReconnectAttempts = 15;
    static constexpr int kReconnectIntervalMs = 2000;
};
