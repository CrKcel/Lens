#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QProcess>

namespace lens {

// 在工作文件夹中执行 shell 命令。execute 为阻塞实现（QProcess::waitForFinished），
// 由 AgentSession 放到线程池调用，不阻塞 GUI；超时强制终止并返回已捕获的输出。
class BashTool final : public IBuiltinTool
{
public:
    QString name() const override { return QStringLiteral("bash"); }
    QString description() const override
    {
        return QStringLiteral("在工作文件夹中执行 shell 命令并返回输出（stdout/stderr 与退出码）");
    }
    nlohmann::json parametersSchema() const override
    {
        return {{"type", "object"},
                {"properties",
                 {{"command", {{"type", "string"}, {"description", "要执行的命令"}}},
                  {"timeout_ms",
                   {{"type", "integer"},
                    {"description", "超时毫秒数（1000~600000，默认 60000），超时后进程被终止"}}}}},
                {"required", {"command"}}};
    }

    ToolResult execute(const nlohmann::json &args, const QString &workdir) override
    {
        const QString command = argString(args, "command");
        if (command.isEmpty())
            return {false, QStringLiteral("缺少 command 参数")};
        const int timeoutMs = qBound(1000, argInt(args, "timeout_ms", 60000), 600000);

        QProcess process;
        process.setWorkingDirectory(QDir(workdir).absolutePath());
        process.start(QStringLiteral("sh"), {QStringLiteral("-c"), command});
        if (!process.waitForStarted(5000))
            return {false, QStringLiteral("无法启动 shell：%1").arg(process.errorString())};

        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(2000);
            return {false,
                    QStringLiteral("命令超时（%1 ms），已终止。已捕获输出：\n%2")
                        .arg(timeoutMs)
                        .arg(collectOutput(process))};
        }

        const int exitCode = process.exitCode();
        QString output = collectOutput(process);
        if (output.size() > 256 * 1024)
            output = output.left(256 * 1024) + QStringLiteral("\n…[输出超过 256KB，已截断]");
        output += QStringLiteral("\n[退出码: %1]").arg(exitCode);
        return {exitCode == 0, output};
    }

private:
    static QString collectOutput(QProcess &process)
    {
        const QByteArray stdoutData = process.readAllStandardOutput();
        const QByteArray stderrData = process.readAllStandardError();
        QString output = QString::fromUtf8(stdoutData);
        const QString err = QString::fromUtf8(stderrData);
        if (!err.isEmpty()) {
            if (!output.isEmpty())
                output += QLatin1Char('\n');
            output += QStringLiteral("[stderr]\n") + err;
        }
        return output.trimmed();
    }
};

} // namespace lens
