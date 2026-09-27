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

} // namespace detail

// Git 状态行（"- Git 分支：..."）。非 Git 仓库、git 不可用或 workdir 为空
// 时返回空串。启动 git 子进程并阻塞等待，单次最长约两个 timeoutMs，因此
// 调用方应在后台线程取得（主线程用 build(workdir, gitLine) 拼装缓存结果）
inline QString gitSummary(const QString &workdir)
{
    if (workdir.trimmed().isEmpty())
        return {};
    const QString branch =
        detail::runCommand(QStringLiteral("git"),
                           {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                            QStringLiteral("HEAD")},
                           workdir.trimmed());
    if (branch.isEmpty())
        return {};
    const QString status =
        detail::runCommand(QStringLiteral("git"), {QStringLiteral("status"), QStringLiteral("--porcelain")},
                           workdir.trimmed());
    const int changes =
        status.isEmpty() ? 0 : int(status.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size());
    return QStringLiteral("- Git 分支：%1（%2）")
        .arg(branch, changes == 0 ? QStringLiteral("工作区干净")
                                  : QStringLiteral("%1 个未提交变更").arg(changes));
}

// 工作环境提示词段落：工作文件夹、操作系统、当前日期与 Git 状态（gitLine 为空
// 则省略 Git 行）。纯计算，不启动子进程，可在主线程调用
inline QString build(const QString &workdir, const QString &gitLine)
{
    QStringList lines;
    lines << QStringLiteral("## 运行环境");
    if (!workdir.trimmed().isEmpty())
        lines << QStringLiteral("- 工作文件夹：%1").arg(workdir.trimmed());
    lines << QStringLiteral("- 操作系统：%1").arg(QSysInfo::prettyProductName());
    lines << QStringLiteral("- 当前日期：%1").arg(QDate::currentDate().toString(Qt::ISODate));
    if (!gitLine.isEmpty())
        lines << gitLine;
    return lines.join(QLatin1Char('\n'));
}

// 一步到位的同步版本：自行取得 Git 行（会启动 git 子进程并阻塞），
// 供测试与后台线程使用，不要在主线程调用
inline QString build(const QString &workdir)
{
    return build(workdir, gitSummary(workdir));
}

} // namespace lens::envprompt
