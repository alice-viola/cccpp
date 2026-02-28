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

QString Config::claudeBinary() const
{
    if (m_data.contains("claude_binary") && m_data["claude_binary"].is_string())
        return QString::fromStdString(m_data["claude_binary"].get<std::string>());
    return "claude";
}

QStringList Config::agentModeFlags() const
{
    return {"--allowedTools", "Bash,Read,Edit,Write,Glob,Grep,Task"};
}

QStringList Config::askModeFlags() const
{
    return {"--tools", "Read,Glob,Grep"};
}

QStringList Config::planModeFlags() const
{
    return {"--permission-mode", "plan"};
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
    save();
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
    save();
}
