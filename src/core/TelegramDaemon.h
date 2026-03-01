#pragma once

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QMap>
#include <QList>

class TelegramApi;
struct TelegramMessage;
struct TelegramCallback;

struct ConnectedInstance {
    QLocalSocket *socket = nullptr;
    QString workspace;
    QString name;
    QByteArray readBuffer;
};

struct DaemonUserState {
    qint64 chatId = 0;
    QString activeWorkspace;
};

class TelegramDaemon : public QObject {
    Q_OBJECT
public:
    explicit TelegramDaemon(QObject *parent = nullptr);
    ~TelegramDaemon();

    bool start();
    void stop();

    static QString serverName();
    static QString lockFilePath();
    static bool isDaemonRunning();
    static bool removeStale();

private:
    void onNewConnection();
    void onInstanceData(QLocalSocket *socket);
    void onInstanceDisconnected(QLocalSocket *socket);
    void onTelegramMessage(const TelegramMessage &msg);
    void onTelegramCallback(const TelegramCallback &cb);

    void routeToInstance(qint64 chatId, const QString &type,
                         const QString &text, const QString &command = {},
                         const QString &args = {});
    void sendToSocket(QLocalSocket *socket, const QByteArray &data);
    ConnectedInstance *findInstance(const QString &workspace);

    // Daemon-level commands
    void handleWsCommand(qint64 chatId);
    void handleSwitchCommand(qint64 chatId, const QString &target);

    void writeLockFile();
    void removeLockFile();
    void checkGracePeriod();

    TelegramApi *m_api = nullptr;
    QLocalServer *m_server = nullptr;
    QTimer *m_graceTimer = nullptr;

    QList<ConnectedInstance> m_instances;
    QMap<qint64, DaemonUserState> m_userStates; // userId -> state
};
