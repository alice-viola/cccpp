#pragma once

#include <QString>
#include <nlohmann/json.hpp>

namespace JsonUtils {

inline QString getString(const nlohmann::json &j, const std::string &key,
                         const QString &defaultVal = {})
{
    if (j.contains(key) && j[key].is_string())
        return QString::fromStdString(j[key].get<std::string>());
    return defaultVal;
}

inline int getInt(const nlohmann::json &j, const std::string &key, int defaultVal = 0)
{
    if (j.contains(key) && j[key].is_number())
        return j[key].get<int>();
    return defaultVal;
}

inline bool getBool(const nlohmann::json &j, const std::string &key, bool defaultVal = false)
{
    if (j.contains(key) && j[key].is_boolean())
        return j[key].get<bool>();
    return defaultVal;
}

} // namespace JsonUtils
