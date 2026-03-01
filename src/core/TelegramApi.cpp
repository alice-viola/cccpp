#include "core/TelegramApi.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHttpMultiPart>
#include <QDebug>

static const QString kBaseUrl = QStringLiteral("https://api.telegram.org/bot%1/%2");
static const int kPollTimeoutSec = 30;
static const int kRetryIntervalMs = 3000;

TelegramApi::TelegramApi(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &TelegramApi::poll);
}

void TelegramApi::setToken(const QString &token)
{
    m_token = token;
}

void TelegramApi::setAllowedUsers(const QList<qint64> &userIds)
{
    m_allowedUsers = userIds;
}

void TelegramApi::startPolling()
{
    if (m_token.isEmpty()) return;
    m_polling = true;
    poll();
}

void TelegramApi::stopPolling()
{
    m_polling = false;
    m_pollTimer->stop();
}

bool TelegramApi::isUserAllowed(qint64 userId) const
{
    if (m_allowedUsers.isEmpty()) return true; // no restrictions if list empty
    return m_allowedUsers.contains(userId);
}

QNetworkReply *TelegramApi::apiCall(const QString &method, const QByteArray &payload)
{
    QUrl url(kBaseUrl.arg(m_token, method));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return m_nam->post(req, payload);
}

// ---- Polling ----

void TelegramApi::poll()
{
    if (!m_polling || m_pollInFlight) return;

    QJsonObject body;
    body["offset"] = m_updateOffset;
    body["timeout"] = kPollTimeoutSec;
    body["allowed_updates"] = QJsonArray({"message", "callback_query"});

    auto *reply = apiCall("getUpdates", QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_pollInFlight = true;

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        handlePollResponse(reply);
        reply->deleteLater();
    });
}

void TelegramApi::handlePollResponse(QNetworkReply *reply)
{
    m_pollInFlight = false;

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[TelegramApi] poll error:" << reply->errorString();
        emit pollingError(reply->errorString());
        if (m_polling) m_pollTimer->start(kRetryIntervalMs);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();
    if (!root["ok"].toBool()) {
        qWarning() << "[TelegramApi] API error:" << root["description"].toString();
        emit pollingError(root["description"].toString());
        if (m_polling) m_pollTimer->start(kRetryIntervalMs);
        return;
    }

    QJsonArray results = root["result"].toArray();
    for (const QJsonValue &val : results) {
        QJsonObject update = val.toObject();
        qint64 updateId = static_cast<qint64>(update["update_id"].toDouble());
        if (updateId >= m_updateOffset)
            m_updateOffset = updateId + 1;

        if (update.contains("message")) {
            QJsonObject msg = update["message"].toObject();
            QJsonObject from = msg["from"].toObject();
            qint64 userId = static_cast<qint64>(from["id"].toDouble());

            if (!isUserAllowed(userId)) continue;

            TelegramMessage tm;
            tm.chatId = static_cast<qint64>(msg["chat"].toObject()["id"].toDouble());
            tm.messageId = static_cast<qint64>(msg["message_id"].toDouble());
            tm.text = msg["text"].toString();
            tm.firstName = from["first_name"].toString();
            tm.userId = userId;

            if (!tm.text.isEmpty())
                emit messageReceived(tm);
        }

        if (update.contains("callback_query")) {
            QJsonObject cb = update["callback_query"].toObject();
            QJsonObject from = cb["from"].toObject();
            qint64 userId = static_cast<qint64>(from["id"].toDouble());

            if (!isUserAllowed(userId)) continue;

            TelegramCallback tc;
            tc.callbackQueryId = cb["id"].toString();
            tc.data = cb["data"].toString();
            tc.userId = userId;
            if (cb.contains("message")) {
                QJsonObject cbMsg = cb["message"].toObject();
                tc.chatId = static_cast<qint64>(cbMsg["chat"].toObject()["id"].toDouble());
                tc.messageId = static_cast<qint64>(cbMsg["message_id"].toDouble());
            }
            emit callbackQueryReceived(tc);
        }
    }

    // Continue polling immediately
    if (m_polling) m_pollTimer->start(0);
}

// ---- Outbound methods ----

void TelegramApi::sendMessage(qint64 chatId, const QString &text,
                              const QString &parseMode,
                              std::function<void(qint64)> callback)
{
    // Telegram limit: 4096 chars per message
    const int kMaxLen = 4096;
    if (text.length() <= kMaxLen) {
        QJsonObject body;
        body["chat_id"] = chatId;
        body["text"] = text;
        if (!parseMode.isEmpty())
            body["parse_mode"] = parseMode;

        auto *reply = apiCall("sendMessage", QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [reply, callback] {
            qint64 msgId = 0;
            if (reply->error() == QNetworkReply::NoError) {
                QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
                if (resp["ok"].toBool())
                    msgId = static_cast<qint64>(resp["result"].toObject()["message_id"].toDouble());
            } else {
                qWarning() << "[TelegramApi] sendMessage error:" << reply->errorString();
            }
            if (callback) callback(msgId);
            reply->deleteLater();
        });
    } else {
        // Split into chunks
        int offset = 0;
        while (offset < text.length()) {
            QString chunk = text.mid(offset, kMaxLen);
            offset += kMaxLen;
            bool isLast = (offset >= text.length());
            auto cb = isLast ? callback : std::function<void(qint64)>(nullptr);

            QJsonObject body;
            body["chat_id"] = chatId;
            body["text"] = chunk;

            auto *reply = apiCall("sendMessage", QJsonDocument(body).toJson(QJsonDocument::Compact));
            connect(reply, &QNetworkReply::finished, this, [reply, cb] {
                qint64 msgId = 0;
                if (reply->error() == QNetworkReply::NoError) {
                    QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
                    if (resp["ok"].toBool())
                        msgId = static_cast<qint64>(resp["result"].toObject()["message_id"].toDouble());
                }
                if (cb) cb(msgId);
                reply->deleteLater();
            });
        }
    }
}

void TelegramApi::editMessage(qint64 chatId, qint64 messageId, const QString &text,
                              const QString &parseMode)
{
    QJsonObject body;
    body["chat_id"] = chatId;
    body["message_id"] = messageId;
    body["text"] = text.left(4096);
    if (!parseMode.isEmpty())
        body["parse_mode"] = parseMode;

    auto *reply = apiCall("editMessageText", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply] {
        if (reply->error() != QNetworkReply::NoError)
            qWarning() << "[TelegramApi] editMessage error:" << reply->errorString();
        reply->deleteLater();
    });
}

void TelegramApi::sendPhoto(qint64 chatId, const QByteArray &imageData,
                            const QString &caption)
{
    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart chatPart;
    chatPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant("form-data; name=\"chat_id\""));
    chatPart.setBody(QByteArray::number(chatId));
    multiPart->append(chatPart);

    QHttpPart photoPart;
    photoPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    photoPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant("form-data; name=\"photo\"; filename=\"image.png\""));
    photoPart.setBody(imageData);
    multiPart->append(photoPart);

    if (!caption.isEmpty()) {
        QHttpPart capPart;
        capPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant("form-data; name=\"caption\""));
        capPart.setBody(caption.toUtf8());
        multiPart->append(capPart);
    }

    QUrl url(kBaseUrl.arg(m_token, "sendPhoto"));
    QNetworkRequest req(url);

    auto *reply = m_nam->post(req, multiPart);
    multiPart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [reply] {
        if (reply->error() != QNetworkReply::NoError)
            qWarning() << "[TelegramApi] sendPhoto error:" << reply->errorString();
        reply->deleteLater();
    });
}

void TelegramApi::answerCallbackQuery(const QString &callbackQueryId, const QString &text)
{
    QJsonObject body;
    body["callback_query_id"] = callbackQueryId;
    if (!text.isEmpty())
        body["text"] = text;

    auto *reply = apiCall("answerCallbackQuery", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply] {
        reply->deleteLater();
    });
}

void TelegramApi::getMe(std::function<void(bool, const QString &)> callback)
{
    auto *reply = apiCall("getMe", "{}");
    connect(reply, &QNetworkReply::finished, this, [reply, callback] {
        bool ok = false;
        QString username;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
            if (resp["ok"].toBool()) {
                ok = true;
                username = resp["result"].toObject()["username"].toString();
            }
        }
        if (callback) callback(ok, username);
        reply->deleteLater();
    });
}
