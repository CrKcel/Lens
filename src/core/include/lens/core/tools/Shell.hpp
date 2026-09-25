#pragma once

#include <QByteArray>
#include <QString>
#include <QStandardPaths>
#include <QStringList>

namespace lens::shell {

// 平台 shell 解析：Windows 用 pwsh(7) > PowerShell 5.1 > cmd，macOS 用 zsh，Linux 用 bash。
struct ShellCommand
{
    QString program;   // 可执行文件完整路径
    QString name;      // 展示名（工具名 / 环境提示词）
};

inline const ShellCommand &resolve()
{
    static const ShellCommand shell = [] {
#if defined(Q_OS_WIN)
        QString pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh"));
        if (pwsh.isEmpty())
            pwsh = QStandardPaths::findExecutable(
                QStringLiteral("pwsh"), {QStringLiteral("C:/Program Files/PowerShell/7")});
        if (!pwsh.isEmpty())
            return ShellCommand{pwsh, QStringLiteral("pwsh")};
        QString powershell = QStandardPaths::findExecutable(QStringLiteral("powershell"));
        if (powershell.isEmpty())
            powershell = QStandardPaths::findExecutable(
                QStringLiteral("powershell"),
                {QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0")});
        if (!powershell.isEmpty())
            return ShellCommand{powershell, QStringLiteral("pwsh")};
        QString cmd = QStandardPaths::findExecutable(QStringLiteral("cmd"));
        if (cmd.isEmpty())
            cmd = QStringLiteral("C:/Windows/System32/cmd.exe");
        return ShellCommand{cmd, QStringLiteral("cmd")};
#elif defined(Q_OS_MACOS)
        QString zsh = QStandardPaths::findExecutable(QStringLiteral("zsh"));
        if (zsh.isEmpty())
            zsh = QStringLiteral("/bin/zsh");
        return ShellCommand{zsh, QStringLiteral("zsh")};
#else
        const QString bash = QStandardPaths::findExecutable(QStringLiteral("bash"));
        if (bash.isEmpty())
            return ShellCommand{QString(), QStringLiteral("bash")};
        return ShellCommand{bash, QStringLiteral("bash")};
#endif
    }();
    return shell;
}

// 启动参数：固定前缀 + 命令文本本身。PowerShell 用 -EncodedCommand（UTF-16LE base64），
// 避免引号转义问题，并前置 OutputEncoding=UTF8 保证输出可按 UTF-8 解码；
// cmd 用 /d /s /c 并前置 chcp 65001，同理保证输出为 UTF-8。
inline QStringList launchArgs(const QString &command)
{
#if defined(Q_OS_WIN)
    const ShellCommand &shell = resolve();
    if (shell.name == QLatin1String("cmd"))
        return {QStringLiteral("/d"), QStringLiteral("/s"), QStringLiteral("/c"),
                QStringLiteral("chcp 65001>nul & ") + command};
    const QString script = QStringLiteral("[Console]::OutputEncoding=[System.Text.Encoding]::UTF8;")
                           + command;
    const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()),
                           int(script.size() * sizeof(char16_t)));
    return {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
            QStringLiteral("-EncodedCommand"), QString::fromLatin1(utf16.toBase64())};
#else
    return {QStringLiteral("-c"), command};
#endif
}

} // namespace lens::shell
