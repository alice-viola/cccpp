#pragma once

#include "core/ChatHandler.h"

class TelegramApi;
struct TelegramMessage;
struct TelegramCallback;

class TelegramBridge : public ChatHandler {
    Q_OBJECT
public:
    explicit TelegramBridge(TelegramApi *api, QObject *parent = nullptr);

protected:
    void doSendResponse(qint64 chatId, const QString &text) override;
    void doSendEditResponse(qint64 chatId, qint64 msgId, const QString &text) override;
    void doSendWithKeyboard(qint64 chatId, const QString &text,
                            const QJsonArray &rows) override;
    void doRequestSendMessage(qint64 chatId, const QString &text,
                              std::function<void(qint64)> cb) override;

private:
    void onMessage(const TelegramMessage &msg);
    void onCallbackQuery(const TelegramCallback &cb);

    TelegramApi *m_api = nullptr;
};
