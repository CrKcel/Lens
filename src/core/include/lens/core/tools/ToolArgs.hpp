#pragma once

#include <QDir>
#include <QString>
#include <nlohmann/json.hpp>

namespace lens {

inline QString argString(const nlohmann::json &args, const char *key,
                         const QString &fallback = QString())
{
    const auto it = args.find(key);
    if (it == args.end() || !it->is_string())
        return fallback;
    return QString::fromStdString(it->get<std::string>());
}

inline int argInt(const nlohmann::json &args, const char *key, int fallback)
{
    const auto it = args.find(key);
    if (it == args.end())
        return fallback;
    if (it->is_number_integer())
        return it->get<int>();
    if (it->is_number_float())
        return static_cast<int>(it->get<double>());
    return fallback;
}

inline bool argBool(const nlohmann::json &args, const char *key, bool fallback)
{
    const auto it = args.find(key);
    if (it == args.end() || !it->is_boolean())
        return fallback;
    return it->get<bool>();
}

// 相对路径以工作文件夹为基准；绝对路径原样使用
inline QString resolveWorkdirPath(const QString &workdir, const QString &path)
{
    if (QDir::isAbsolutePath(path))
        return QDir::cleanPath(path);
    return QDir::cleanPath(QDir(workdir).absoluteFilePath(path));
}

} // namespace lens
