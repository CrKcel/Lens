#pragma once

#include "lens/core/tools/BuiltinTool.hpp"
#include "lens/core/tools/Shell.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryFile>

#if defined(Q_OS_UNIX)
#include <signal.h>
#include <unistd.h>
#endif

namespace lens {

// 在工作文件夹中执行 shell 命令。execute 为阻塞实现（QProcess::waitFor*），
// 由 AgentSession 放到线程池调用，不阻塞 GUI。
// 输出增量捕获，内存只保留末尾 50KB（报错信息通常集中在尾部）；总量超限时
// 完整输出流式转存到临时文件，模型可用 read 工具按路径继续查看。
// 超时终止整棵子进程树（Unix 上子进程 setpgid 独立成组后 kill(-pid)，
// Windows 用 taskkill /T），避免 sleep / dev server 等孙进程在超时后残留。
// shell 由平台决定（见 Shell.hpp）：Windows pwsh/cmd、macOS zsh、Linux bash，
// 工具名跟随实际解析出的 shell。
class BashTool final : public IBuiltinTool
{
public:
    QString name() const override { return shell::resolve().name; }
    QString description() const override
    {
        return QStringLiteral("在工作文件夹中用 %1 执行命令并返回输出（stdout/stderr 与退出码）")
            .arg(shell::resolve().name);
    }
    nlohmann::json parametersSchema() const override
    {
        return {{"type", "object"},
                {"properties",
                 {{"command", {{"type", "string"}, {"description", "要执行的命令"}}},
                  {"timeout_ms",
                   {{"type", "integer"},
                    {"description",
                     "超时毫秒数（1000~600000，默认 60000），超时后终止命令及其全部子进程"}}}}},
                {"required", {"command"}}};
    }

    ToolResult execute(const nlohmann::json &args, const QString &workdir) override
    {
        const QString command = argString(args, "command");
        if (command.isEmpty())
            return {false, QStringLiteral("缺少 command 参数")};
        const int timeoutMs = qBound(1000, argInt(args, "timeout_ms", 60000), 600000);

        const shell::ShellCommand shell = shell::resolve();
        if (shell.program.isEmpty())
            return {false, QStringLiteral("未找到可用的 shell（%1）").arg(shell.name)};

        const QString cwd = QDir(workdir).absolutePath();
        if (!QFileInfo(cwd).isDir())
            return {false,
                    QStringLiteral("工作目录不存在：%1\n无法执行命令，请确认工作文件夹设置。")
                        .arg(cwd)};

        QProcess process;
#if defined(Q_OS_UNIX)
        // 独立进程组：超时后可 kill(-pid) 终止整棵子进程树
        process.setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
        process.setWorkingDirectory(cwd);
        process.start(shell.program, shell::launchArgs(command));
        if (!process.waitForStarted(5000))
            return {false, QStringLiteral("无法启动 shell：%1").arg(process.errorString())};

        Capture capture;
        QElapsedTimer elapsed;
        elapsed.start();
        bool timedOut = false;
        while (true) {
            capture.push(process.readAllStandardOutput());
            capture.push(process.readAllStandardError());
            if (process.waitForFinished(50))
                break;
            if (elapsed.elapsed() >= timeoutMs) {
                killProcessTree(process);
                timedOut = true;
                process.waitForFinished(2000);
                break;
            }
        }
        capture.push(process.readAllStandardOutput()); // 退出/终止后的残余输出
        capture.push(process.readAllStandardError());

        QString output = capture.decoded().trimmed();
        if (capture.spilled())
            output += QStringLiteral("\n[输出共 %1 字节，仅保留末尾 %2KB；完整输出：%3]")
                          .arg(capture.total())
                          .arg(kMaxOutputBytes / 1024)
                          .arg(capture.spillPath());
        if (output.isEmpty())
            output = QStringLiteral("(无输出)");

        if (timedOut)
            return {false,
                    QStringLiteral("命令超时（%1 ms），已终止命令及其全部子进程。已捕获输出：\n%2")
                        .arg(timeoutMs)
                        .arg(output)};
        if (process.exitStatus() == QProcess::CrashExit)
            return {false, QStringLiteral("%1\n[shell 进程异常终止]").arg(output)};

        output += QStringLiteral("\n[退出码: %1]").arg(process.exitCode());
        return {process.exitCode() == 0, output};
    }

private:
    static constexpr qint64 kMaxOutputBytes = 50 * 1024;

    // 增量输出累积：内存只留末尾 kMaxOutputBytes；一旦超限，全部输出按序写入临时文件
    class Capture
    {
    public:
        ~Capture()
        {
            if (m_spill) {
                m_spill->close();
                delete m_spill; // setAutoRemove(false)，文件保留供后续读取
            }
        }
        void push(const QByteArray &chunk)
        {
            if (chunk.isEmpty())
                return;
            m_total += chunk.size();
            if (m_spill)
                m_spill->write(chunk);
            m_tail += chunk;
            if (m_tail.size() > kMaxOutputBytes) {
                if (!m_spill) {
                    m_spill = new QTemporaryFile(
                        QDir::temp().filePath(QStringLiteral("lens-bash-XXXXXX.log")));
                    m_spill->setAutoRemove(false);
                    if (!m_spill->open()) {
                        delete m_spill; // 转存失败不致命，退化为仅保留尾部
                        m_spill = nullptr;
                    } else {
                        m_spill->write(m_tail); // 首次转存：包含当前 chunk 在内的全部历史
                    }
                }
                m_tail.remove(0, m_tail.size() - kMaxOutputBytes);
            }
        }
        QString decoded() const { return QString::fromUtf8(m_tail); }
        bool spilled() const { return m_spill != nullptr; }
        QString spillPath() const { return m_spill ? m_spill->fileName() : QString(); }
        qint64 total() const { return m_total; }

    private:
        QByteArray m_tail;
        QTemporaryFile *m_spill = nullptr;
        qint64 m_total = 0;
    };

    static void killProcessTree(QProcess &process)
    {
#if defined(Q_OS_UNIX)
        const pid_t pid = static_cast<pid_t>(process.processId());
        if (pid <= 0)
            return;
        if (::kill(-pid, SIGKILL) != 0) // 先按进程组终止（覆盖孙进程）
            ::kill(pid, SIGKILL);
#else
        QProcess::startDetached(QStringLiteral("taskkill"),
                                {QStringLiteral("/F"), QStringLiteral("/T"),
                                 QStringLiteral("/PID"),
                                 QString::number(process.processId())});
        process.kill();
#endif
    }
};

} // namespace lens
