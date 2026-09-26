#pragma once

#include <QDir>
#include <QString>
#include <nlohmann/json.hpp>

namespace lens {

inline QString argString(const nlohmann::json &args, const char *key,
                         const QString &fallback = QString())
{
    const auto it = args.find(key);
    if (it == args.end())
        return fallback;
    if (it->is_string()) {
        const std::string raw = it->get<std::string>();
        QString result = QString::fromStdString(raw);
        // QString::fromUtf8 会剥掉开头的 U+FEFF（BOM）；写文件场景需要原样保留
        if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB
            && (unsigned char)raw[2] == 0xBF)
            result.prepend(QChar(0xFEFF));
        return result;
    }
    // 模型偶尔会把数字/布尔当字符串传（如 "42"），做宽松纠正而不是静默失败
    if (it->is_number_integer())
        return QString::number(it->get<long long>());
    if (it->is_number_float())
        return QString::number(it->get<double>());
    if (it->is_boolean())
        return it->get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    return fallback;
}

// required 标量参数存在性检查：缺失或对象/数组/null 都算不通过
inline bool hasScalarArg(const nlohmann::json &args, const char *key)
{
    const auto it = args.find(key);
    if (it == args.end())
        return false;
    return it->is_string() || it->is_number_integer() || it->is_number_float()
        || it->is_boolean();
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

// 相对路径以工作文件夹为基准；绝对路径原样使用；~ 展开为用户主目录
inline QString expandTilde(const QString &path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + QLatin1Char('/') + path.mid(2);
    return path;
}

inline QString resolveWorkdirPath(const QString &workdir, const QString &path)
{
    const QString expanded = expandTilde(path);
    if (QDir::isAbsolutePath(expanded))
        return QDir::cleanPath(expanded);
    return QDir::cleanPath(QDir(workdir).absoluteFilePath(expanded));
}

} // namespace lens
