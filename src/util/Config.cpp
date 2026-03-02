#include "util/Config.h"
#include <QDir>
#include <QFile>

Config &Config::instance()
{
    static Config cfg;
    return cfg;
}

Config::Config()
{
    load();
}

bool Config::load(const QString &path)
{
    m_configPath = path;
    if (m_configPath.isEmpty())
        m_configPath = QDir::homePath() + "/.cccpp/config.json";

    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    try {
        m_data = nlohmann::json::parse(file.readAll().toStdString());
    } catch (...) {
        m_data = nlohmann::json::object();
        return false;
    }
    return true;
}

void Config::save()
{
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    QFile file(m_configPath);
    if (file.open(QIODevice::WriteOnly)) {
        auto str = m_data.dump(2);
        file.write(str.c_str(), static_cast<qint64>(str.size()));
    }
}

void Config::setSuppressAutoSave(bool suppress)
{
    m_suppressSave = suppress;
}

void Config::autoSave()
{
    if (!m_suppressSave)
        save();
}

QString Config::claudeBinary() const
{
    if (m_data.contains("claude_binary") && m_data["claude_binary"].is_string())
        return QString::fromStdString(m_data["claude_binary"].get<std::string>());
    return "claude";
}

void Config::setClaudeBinary(const QString &path)
{
    m_data["claude_binary"] = path.toStdString();
    autoSave();
}

QString Config::theme() const
{
    if (m_data.contains("theme") && m_data["theme"].is_string())
        return QString::fromStdString(m_data["theme"].get<std::string>());
    return "dark";
}

void Config::setTheme(const QString &theme)
{
    m_data["theme"] = theme.toStdString();
    autoSave();
}

QString Config::lastWorkspace() const
{
    if (m_data.contains("last_workspace") && m_data["last_workspace"].is_string())
        return QString::fromStdString(m_data["last_workspace"].get<std::string>());
    return {};
}

void Config::setLastWorkspace(const QString &path)
{
    m_data["last_workspace"] = path.toStdString();
    autoSave();
}

bool Config::telegramEnabled() const
{
    if (m_data.contains("telegram_enabled") && m_data["telegram_enabled"].is_boolean())
        return m_data["telegram_enabled"].get<bool>();
    return false;
}

void Config::setTelegramEnabled(bool enabled)
{
    m_data["telegram_enabled"] = enabled;
    autoSave();
}

QString Config::telegramBotToken() const
{
    if (m_data.contains("telegram_bot_token") && m_data["telegram_bot_token"].is_string())
        return QString::fromStdString(m_data["telegram_bot_token"].get<std::string>());
    return {};
}

void Config::setTelegramBotToken(const QString &token)
{
    m_data["telegram_bot_token"] = token.toStdString();
    autoSave();
}

QList<qint64> Config::telegramAllowedUsers() const
{
    QList<qint64> result;
    if (m_data.contains("telegram_allowed_users") && m_data["telegram_allowed_users"].is_array()) {
        for (const auto &v : m_data["telegram_allowed_users"]) {
            if (v.is_number_integer())
                result.append(v.get<qint64>());
        }
    }
    return result;
}

void Config::setTelegramAllowedUsers(const QList<qint64> &users)
{
    auto arr = nlohmann::json::array();
    for (qint64 id : users)
        arr.push_back(id);
    m_data["telegram_allowed_users"] = arr;
    autoSave();
}
