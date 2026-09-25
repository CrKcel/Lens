#pragma once

#include <QDate>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QSysInfo>

namespace lens::envprompt {
namespace detail {

// 运行一次性命令并返回标准输出（启动失败、超时或非零退出返回空）
inline QString runCommand(const QString &program, const QStringList &args,
                          const QString &workdir, int timeoutMs = 3000)
{
    QProcess process;
    process.setWorkingDirectory(workdir);
    process.start(program, args);
    if (!process.waitForStarted(timeoutMs) || !process.waitForFinished(timeoutMs))
        return {};
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

inline QString gitSummary(const QString &workdir)
{
    const QString branch =
        runCommand(QStringLiteral("git"),
                   {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                    QStringLiteral("HEAD")},
                   workdir);
    if (branch.isEmpty())
        return {};
    const QString status =
        runCommand(QStringLiteral("git"), {QStringLiteral("status"), QStringLiteral("--porcelain")},
                   workdir);
    const int changes =
        status.isEmpty() ? 0 : int(status.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size());
    return QStringLiteral("- Git 分支：%1（%2）")
        .arg(branch, changes == 0 ? QStringLiteral("工作区干净")
                                  : QStringLiteral("%1 个未提交变更").arg(changes));
}

} // namespace detail

// 工作环境提示词段落：操作系统、当前日期、Shell 与 Git 状态。
// workdir 不是 Git 仓库（或 git 不可用）时省略 Git 行。
inline QString build(const QString &workdir)
{
    QStringList lines;
    lines << QStringLiteral("## 运行环境");
    lines << QStringLiteral("- 操作系统：%1").arg(QSysInfo::prettyProductName());
    lines << QStringLiteral("- 当前日期：%1").arg(QDate::currentDate().toString(Qt::ISODate));
    const QString shell = qEnvironmentVariable("SHELL");
    if (!shell.isEmpty())
        lines << QStringLiteral("- Shell：%1").arg(shell);
    if (!workdir.trimmed().isEmpty()) {
        const QString git = detail::gitSummary(workdir.trimmed());
        if (!git.isEmpty())
            lines << git;
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace lens::envprompt
