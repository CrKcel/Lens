#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QFile>

namespace lens {

// 文本替换式编辑：old_string 唯一匹配时替换；多处匹配要求 replace_all
// 或提供更长上下文——避免模型盲目替换。old_string 与 new_string 相同视为错误
// （无变化替换往往是模型复制出错）。BOM 与 CRLF 行尾在匹配后原样保留：
// 匹配在 LF 归一化空间进行，写回时按原文件行尾还原。
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
        if (!hasScalarArg(args, "new_string"))
            return {false, QStringLiteral("缺少 new_string 参数")};
        if (oldString == newString)
            return {false, QStringLiteral("old_string 与 new_string 相同，替换不会产生任何变化")};
        const QString resolved = resolveWorkdirPath(workdir, path);

        QFile file(resolved);
        if (!file.open(QIODevice::ReadOnly))
            return {false, QStringLiteral("无法读取文件 %1：%2").arg(resolved, file.errorString())};
        QByteArray raw = file.readAll();
        file.close();

        const bool hasBom = raw.startsWith("\xEF\xBB\xBF");
        if (hasBom)
            raw.remove(0, 3);
        const bool hasCrlf = raw.contains("\r\n");
        QString text = QString::fromUtf8(raw);
        text.replace(QStringLiteral("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));

        const QString oldNormalized = normalize(oldString);
        const QString newNormalized = normalize(newString);

        int occurrences = 0;
        for (int pos = 0; (pos = text.indexOf(oldNormalized, pos)) >= 0; pos += oldNormalized.size())
            ++occurrences;
        if (occurrences == 0)
            return {false, QStringLiteral("old_string 未在 %1 中找到（需逐字匹配）").arg(resolved)};
        if (occurrences > 1 && !replaceAll)
            return {false, QStringLiteral("old_string 在 %1 中出现 %2 次：请提供更长上下文，或设置 replace_all=true")
                               .arg(resolved).arg(occurrences)};

        if (replaceAll)
            text.replace(oldNormalized, newNormalized);
        else
            text.replace(text.indexOf(oldNormalized), oldNormalized.size(), newNormalized);

        QString output = text;
        if (hasCrlf)
            output.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
        QByteArray payload = output.toUtf8();
        if (hasBom)
            payload.prepend("\xEF\xBB\xBF");

        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {false, QStringLiteral("无法写入文件 %1：%2").arg(resolved, file.errorString())};
        if (file.write(payload) != payload.size())
            return {false, QStringLiteral("写入 %1 失败：%2").arg(resolved, file.errorString())};
        return {true,
                QStringLiteral("已编辑 %1（替换 %2 处）")
                    .arg(resolved)
                    .arg((occurrences > 1 && replaceAll) ? occurrences : 1)};
    }

private:
    static QString normalize(const QString &s)
    {
        QString result = s;
        result.replace(QStringLiteral("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
        return result;
    }
};

} // namespace lens
