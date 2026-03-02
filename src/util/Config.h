#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <nlohmann/json.hpp>

class Config : public QObject {
    Q_OBJECT
public:
    static Config &instance();

    bool load(const QString &path = {});
    void save();
    void setSuppressAutoSave(bool suppress);

    QString claudeBinary() const;
    void setClaudeBinary(const QString &path);
    QString theme() const;
    void setTheme(const QString &theme);
    QString lastWorkspace() const;
    void setLastWorkspace(const QString &path);

    bool telegramEnabled() const;
    void setTelegramEnabled(bool enabled);
    QString telegramBotToken() const;
    void setTelegramBotToken(const QString &token);
    QList<qint64> telegramAllowedUsers() const;
    void setTelegramAllowedUsers(const QList<qint64> &users);

private:
    Config();
    void autoSave();
    QString m_configPath;
    nlohmann::json m_data;
    bool m_suppressSave = false;
};
