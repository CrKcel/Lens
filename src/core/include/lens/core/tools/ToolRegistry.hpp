#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QList>
#include <QSet>
#include <QString>
#include <memory>
#include <vector>

namespace lens {

// 工具注册表。禁用集合中的工具不进入 specs / 请求体（上下文），execute 也拒绝执行
class ToolRegistry
{
public:
    void registerTool(std::shared_ptr<IBuiltinTool> tool)
    {
        m_tools.push_back(std::move(tool));
    }

    // 移除指定名称的工具（MCP 服务器热重载用），未知名称忽略。
    // 只允许在无工具执行进行中时调用（工具执行在 QThreadPool 线程上遍历 m_tools）
    void removeTool(const QString &name)
    {
        std::erase_if(m_tools,
                      [&name](const auto &tool) { return tool->name() == name; });
    }

    // 禁用的工具名集合；传空集合恢复全部启用
    void setDisabledTools(const QSet<QString> &names) { m_disabled = names; }

    bool isEnabled(const QString &name) const { return !m_disabled.contains(name); }

    std::vector<ToolSpec> specs() const
    {
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
        for (const auto &tool : m_tools) {
            if (tool->name() == name) {
                if (m_disabled.contains(name))
                    return {false, QStringLiteral("工具 %1 已被禁用").arg(name)};
                return tool->execute(args, workdir);
            }
        }
        return {false, QStringLiteral("未知工具：%1").arg(name)};
    }

    // OpenAI function-calling 格式的 tools 数组；其它协议格式由 Provider 适配层转换
    nlohmann::json toChatCompletionsTools() const
    {
        auto array = nlohmann::json::array();
        for (const auto &tool : m_tools) {
            if (m_disabled.contains(tool->name()))
                continue;
            const ToolSpec spec = tool->spec();
            array.push_back({
                {"type", "function"},
                {"function",
                 {{"name", spec.name.toStdString()},
                  {"description", spec.description.toStdString()},
                  {"parameters", spec.parameters}}},
            });
        }
        return array;
    }

private:
    std::vector<std::shared_ptr<IBuiltinTool>> m_tools;
    QSet<QString> m_disabled;
};

} // namespace lens
