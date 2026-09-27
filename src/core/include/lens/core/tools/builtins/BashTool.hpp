#pragma once

#include "lens/core/tools/BuiltinTool.hpp"
#include "lens/core/tools/Shell.hpp"

class QProcess;

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
    nlohmann::json parametersSchema() const override;

    ToolResult execute(const nlohmann::json &args, const QString &workdir) override;

private:
    static constexpr qint64 kMaxOutputBytes = 50 * 1024;

    // 增量输出累积：内存只留末尾 kMaxOutputBytes；一旦超限，全部输出按序写入临时文件
    class Capture;
    static void killProcessTree(QProcess &process);
};

} // namespace lens
