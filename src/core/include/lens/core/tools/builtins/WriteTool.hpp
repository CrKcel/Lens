#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QDir>
#include <QFile>

namespace lens {

// 写文件（整体覆盖，父目录不存在时自动创建）
class WriteTool final : public IBuiltinTool
{
public:
    QString name() const override { return QStringLiteral("write"); }
    QString description() const override
    {
        return QStringLiteral("将内容写入文件（整体覆盖，可创建新文件）");
    }
    nlohmann::json parametersSchema() const override
    {
        return {{"type", "object"},
                {"properties",
                 {{"path",
                   {{"type", "string"},
                    {"description", "文件路径，相对路径以工作文件夹为基准"}}},
                  {"content", {{"type", "string"}, {"description", "完整文件内容（UTF-8）"}}}}},
                {"required", {"path", "content"}}};
    }

    ToolResult execute(const nlohmann::json &args, const QString &workdir) override
    {
        const QString path = argString(args, "path");
        if (path.isEmpty())
            return {false, QStringLiteral("缺少 path 参数")};
        const QString content = argString(args, "content");
        const QString resolved = resolveWorkdirPath(workdir, path);

        const QString parent = QFileInfo(resolved).absolutePath();
        if (!QDir().mkpath(parent))
            return {false, QStringLiteral("无法创建目录 %1").arg(parent)};

        QFile file(resolved);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {false, QStringLiteral("无法写入文件 %1：%2").arg(resolved, file.errorString())};
        const QByteArray data = content.toUtf8();
        if (file.write(data) != data.size())
            return {false, QStringLiteral("写入 %1 失败：%2").arg(resolved, file.errorString())};
        return {true, QStringLiteral("已写入 %1（%2 字节）").arg(resolved).arg(data.size())};
    }
};

} // namespace lens
