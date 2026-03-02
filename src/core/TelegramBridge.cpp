#include "core/TelegramBridge.h"
#include "core/TelegramApi.h"

#include <QJsonObject>

TelegramBridge::TelegramBridge(TelegramApi *api, QObject *parent)
    : ChatHandler(parent)
    , m_api(api)
{
    connect(m_api, &TelegramApi::messageReceived,
            this, &TelegramBridge::onMessage);
    connect(m_api, &TelegramApi::callbackQueryReceived,
            this, &TelegramBridge::onCallbackQuery);
}

void TelegramBridge::onMessage(const TelegramMessage &msg)
{
    onChatMessage(msg.chatId, msg.text);
}

void TelegramBridge::onCallbackQuery(const TelegramCallback &cb)
{
    m_api->answerCallbackQuery(cb.callbackQueryId);
    onChatCallback(cb.chatId, cb.callbackQueryId, cb.data);
}

void TelegramBridge::doSendResponse(qint64 chatId, const QString &text)
{
    m_api->sendMessage(chatId, text, {});
}

void TelegramBridge::doSendEditResponse(qint64 chatId, qint64 msgId, const QString &text)
{
    m_api->editMessage(chatId, msgId, text, {});
}

void TelegramBridge::doSendWithKeyboard(qint64 chatId, const QString &text,
                                        const QJsonArray &rows)
{
    QJsonObject markup;
    markup["inline_keyboard"] = rows;
    m_api->sendMessage(chatId, text, {}, nullptr, markup);
}

void TelegramBridge::doRequestSendMessage(qint64 chatId, const QString &text,
                                          std::function<void(qint64)> cb)
{
    m_api->sendMessage(chatId, text, {}, cb);
}
