#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>

struct TelegramMessage {
    qint64 chatId = 0;
    qint64 messageId = 0;
    QString text;
    QString firstName;
    qint64 userId = 0;
};

struct TelegramCallback {
    QString callbackQueryId;
    qint64 chatId = 0;
    qint64 messageId = 0;
    QString data;
    qint64 userId = 0;
};

class TelegramApi : public QObject {
    Q_OBJECT
public:
    explicit TelegramApi(QObject *parent = nullptr);

    void setToken(const QString &token);
    void setAllowedUsers(const QList<qint64> &userIds);

    void startPolling();
    void stopPolling();
    bool isPolling() const { return m_polling; }

    // Bot API methods — all return the message_id on success via callback
    void sendMessage(qint64 chatId, const QString &text,
                     const QString &parseMode = {},
                     std::function<void(qint64 messageId)> callback = nullptr,
                     const QJsonObject &replyMarkup = {});
    void editMessage(qint64 chatId, qint64 messageId, const QString &text,
                     const QString &parseMode = "Markdown");
    void sendPhoto(qint64 chatId, const QByteArray &imageData,
                   const QString &caption = {});
    void answerCallbackQuery(const QString &callbackQueryId,
                             const QString &text = {});

    // Utility: call getMe to verify the token
    void getMe(std::function<void(bool ok, const QString &username)> callback);

signals:
    void messageReceived(const TelegramMessage &msg);
    void callbackQueryReceived(const TelegramCallback &cb);
    void pollingError(const QString &error);

private:
    void poll();
    void handlePollResponse(QNetworkReply *reply);
    QNetworkReply *apiCall(const QString &method, const QByteArray &payload);
    bool isUserAllowed(qint64 userId) const;

    void abortStalePoll();

    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_watchdog = nullptr;
    QNetworkReply *m_pollReply = nullptr;
    QString m_token;
    QList<qint64> m_allowedUsers;
    qint64 m_updateOffset = 0;
    bool m_polling = false;
    bool m_pollInFlight = false;
};
