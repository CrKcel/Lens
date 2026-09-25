#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QDir>
#include <QFile>

namespace lens {

// 文本替换式编辑：old_string 唯一匹配时替换；多处匹配要求 replace_all
// 或提供更长上下文——避免模型盲目替换。
class EditTool final : public IBuiltinTool
{
public:
    QString name() const override { return QStringLiteral("edit"); }
    QString description() const override
    {
        return QStringLiteral("把文件中的 old_string 替换为 new_string（要求唯一匹配或 replace_all）");
    }
    nlohmann::json parametersSchema() const override
    {
        return {{"type", "object"},
                {"properties",
                 {{"path",
                   {{"type", "string"},
                    {"description", "文件路径，相对路径以工作文件夹为基准"}}},
                  {"old_string", {{"type", "string"}, {"description", "要被替换的原文片段，需与文件内容逐字一致"}}},
                  {"new_string", {{"type", "string"}, {"description", "替换后的内容"}}},
                  {"replace_all", {{"type", "boolean"}, {"description", "替换全部匹配，默认 false"}}}}},
                {"required", {"path", "old_string", "new_string"}}};
    }

    ToolResult execute(const nlohmann::json &args, const QString &workdir) override
    {
        const QString path = argString(args, "path");
        const QString oldString = argString(args, "old_string");
        const QString newString = argString(args, "new_string");
        const bool replaceAll = argBool(args, "replace_all", false);
        if (path.isEmpty() || oldString.isEmpty())
            return {false, QStringLiteral("缺少 path / old_string 参数")};
        const QString resolved = resolveWorkdirPath(workdir, path);

        QFile file(resolved);
        if (!file.open(QIODevice::ReadOnly))
            return {false, QStringLiteral("无法读取文件 %1：%2").arg(resolved, file.errorString())};
        QString text = QString::fromUtf8(file.readAll());
        file.close();

        int occurrences = 0;
        for (int pos = 0; (pos = text.indexOf(oldString, pos)) >= 0; pos += oldString.size())
            ++occurrences;
        if (occurrences == 0)
            return {false, QStringLiteral("old_string 未在 %1 中找到（需逐字匹配）").arg(resolved)};
        if (occurrences > 1 && !replaceAll)
            return {false, QStringLiteral("old_string 在 %1 中出现 %2 次：请提供更长上下文，或设置 replace_all=true")
                               .arg(resolved).arg(occurrences)};

        int replaced = 0;
        if (replaceAll) {
            text.replace(oldString, newString);
            replaced = occurrences;
        } else {
            const int pos = text.indexOf(oldString);
            text.replace(pos, oldString.size(), newString);
            replaced = 1;
        }

        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {false, QStringLiteral("无法写入文件 %1：%2").arg(resolved, file.errorString())};
        file.write(text.toUtf8());
        return {true, QStringLiteral("已编辑 %1（替换 %2 处）").arg(resolved).arg(replaced)};
    }
};

} // namespace lens
