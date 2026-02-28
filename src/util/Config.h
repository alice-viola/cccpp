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

    QString claudeBinary() const;
    QStringList agentModeFlags() const;
    QStringList askModeFlags() const;
    QStringList planModeFlags() const;
    QString theme() const;
    void setTheme(const QString &theme);
    QString lastWorkspace() const;
    void setLastWorkspace(const QString &path);

private:
    Config();
    QString m_configPath;
    nlohmann::json m_data;
};
