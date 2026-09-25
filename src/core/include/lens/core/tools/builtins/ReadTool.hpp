#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QFile>
#include <QStringList>

namespace lens {

// 读取文本文件。offset/limit 按 1 起始的行号选取，默认读前 2000 行；
// 超过 256KB 截断。输出为纯文本，便于 edit 工具用原文片段匹配。
class ReadTool final : public IBuiltinTool
{
public:
    QString name() const override { return QStringLiteral("read"); }
    QString description() const override
    {
        return QStringLiteral("读取工作文件夹中的文本文件内容");
    }
    nlohmann::json parametersSchema() const override
    {
        return {{"type", "object"},
                {"properties",
                 {{"path",
                   {{"type", "string"},
                    {"description", "文件路径，相对路径以工作文件夹为基准"}}},
                  {"offset", {{"type", "integer"}, {"description", "起始行号（从 1 开始），默认 1"}}},
                  {"limit", {{"type", "integer"}, {"description", "最多读取行数，默认 2000"}}}}},
                {"required", {"path"}}};
    }

    ToolResult execute(const nlohmann::json &args, const QString &workdir) override
    {
        const QString path = argString(args, "path");
        if (path.isEmpty())
            return {false, QStringLiteral("缺少 path 参数")};
        const QString resolved = resolveWorkdirPath(workdir, path);

        QFile file(resolved);
        if (!file.open(QIODevice::ReadOnly))
            return {false, QStringLiteral("无法读取文件 %1：%2").arg(resolved, file.errorString())};

        const qint64 maxSize = 256 * 1024;
        QString text = QString::fromUtf8(file.read(maxSize));
        const bool sizeTruncated = file.bytesAvailable() > 0;
        file.close();

        QStringList lines = text.split(QLatin1Char('\n'));
        const int totalLines = lines.size();
        const int offset = qBound(1, argInt(args, "offset", 1), totalLines);
        const int limit = qBound(1, argInt(args, "limit", 2000), 5000);

        QStringList selected;
        for (int i = offset - 1; i < qMin(offset - 1 + limit, totalLines); ++i)
            selected.append(lines.at(i));

        QString output = selected.join(QLatin1Char('\n'));
        if (sizeTruncated)
            output += QStringLiteral("\n…[内容超过 256KB，已截断]");
        if (offset - 1 + limit < totalLines)
            output += QStringLiteral("\n…[仅显示第 %1~%2 行，共 %3 行，可用 offset/limit 继续读取]")
                          .arg(offset).arg(offset - 1 + limit).arg(totalLines);
        return {true, output};
    }
};

} // namespace lens
