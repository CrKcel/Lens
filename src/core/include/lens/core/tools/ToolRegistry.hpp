#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QString>
#include <memory>
#include <vector>

namespace lens {

// 工具注册表。禁用集合中的工具不进入 specs / 请求体（上下文），execute 也拒绝执行。
// 线程安全：execute 在 QThreadPool 线程上查找并执行工具，MCP 热重载在主线程增删——
// m_tools/m_disabled 的全部访问走互斥量（工具本体在锁外执行，长时间任务不阻塞
// 注册表；执行中的工具由 shared_ptr 保活，随后被移除也安全，过期结果由
// AgentSession 的代际守卫丢弃）
class ToolRegistry
{
public:
    void registerTool(std::shared_ptr<IBuiltinTool> tool)
    {
        const QMutexLocker locker(&m_mutex);
        m_tools.push_back(std::move(tool));
    }

    // 移除指定名称的工具（MCP 服务器热重载用），未知名称忽略
    void removeTool(const QString &name)
    {
        const QMutexLocker locker(&m_mutex);
        std::erase_if(m_tools,
                      [&name](const auto &tool) { return tool->name() == name; });
    }

    // 禁用的工具名集合；传空集合恢复全部启用
    void setDisabledTools(const QSet<QString> &names)
    {
        const QMutexLocker locker(&m_mutex);
        m_disabled = names;
    }

    bool isEnabled(const QString &name) const
    {
        const QMutexLocker locker(&m_mutex);
        return !m_disabled.contains(name);
    }

    std::vector<ToolSpec> specs() const
    {
        const QMutexLocker locker(&m_mutex);
        std::vector<ToolSpec> result;
        result.reserve(m_tools.size());
        for (const auto &tool : m_tools) {
            if (m_disabled.contains(tool->name()))
                continue;
            result.push_back(tool->spec());
        }
        return result;
    }

    ToolResult execute(const QString &name, const nlohmann::json &args,
                       const QString &workdir) const
    {
        std::shared_ptr<IBuiltinTool> tool;
        bool disabled = false;
        {
            const QMutexLocker locker(&m_mutex);
            for (const auto &candidate : m_tools) {
                if (candidate->name() == name) {
                    tool = candidate;
                    disabled = m_disabled.contains(name);
                    break;
                }
            }
        }
        if (!tool)
            return {false, QStringLiteral("未知工具：%1").arg(name)};
        if (disabled)
            return {false, QStringLiteral("工具 %1 已被禁用").arg(name)};
        return tool->execute(args, workdir); // 锁外执行
    }

private:
    mutable QMutex m_mutex;
    std::vector<std::shared_ptr<IBuiltinTool>> m_tools;
    QSet<QString> m_disabled;
};

// 内置工具预设 → 禁用集合。full 全部启用；chat 全禁（纯对话）；
// read_only 仅 read；custom 取 customEnabled 清单里列出的工具。
// MCP 工具不在 builtinNames 里，天然不受预设影响
inline QSet<QString> disabledToolsForPreset(const QString &preset,
                                            const QStringList &builtinNames,
                                            const QStringList &customEnabled)
{
    QSet<QString> enabled;
    if (preset == QLatin1String("read_only")) {
        enabled.insert(QStringLiteral("read"));
    } else if (preset == QLatin1String("custom")) {
        for (const QString &name : customEnabled)
            enabled.insert(name);
    } else if (preset != QLatin1String("chat")) {
        for (const QString &name : builtinNames) // full（默认）
            enabled.insert(name);
    }
    QSet<QString> disabled;
    for (const QString &name : builtinNames) {
        if (!enabled.contains(name))
            disabled.insert(name);
    }
    return disabled;
}

} // namespace lens
