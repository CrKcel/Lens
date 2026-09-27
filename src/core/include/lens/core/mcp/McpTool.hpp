#pragma once

#include "lens/core/mcp/McpClient.hpp"
#include "lens/core/tools/BuiltinTool.hpp"

#include <QMutex>
#include <memory>

namespace lens::mcp {

// 把一个 MCP 远程工具桥接成 IBuiltinTool：execute() 转发到共享的 McpClient。
// 同一服务器的工具共享一个客户端；工具循环串行执行，配置读取用互斥量保护。
// 对外名字带 mcp_<服务器>_ 前缀（否则多服务器暴露的同名工具会在注册表里互相
// 遮蔽——execute 只命中第一个），转发给服务器时用其原始工具名
class McpTool : public IBuiltinTool
{
public:
    McpTool(QString serverName, McpClient::ToolInfo info, std::shared_ptr<McpClient> client)
        : m_serverName(std::move(serverName))
        , m_info(std::move(info))
        , m_client(std::move(client))
    {
    }

    // 桥接后的对外工具名（注册表 / 请求体 / 上下文检查器共用同一规则）
    static QString exposedName(const QString &serverName, const QString &toolName)
    {
        return QStringLiteral("mcp_%1_%2").arg(serverName, toolName);
    }

    QString name() const override { return exposedName(m_serverName, m_info.name); }
    QString description() const override { return m_info.description; }
    nlohmann::json parametersSchema() const override
    {
        return m_info.inputSchema.is_object() ? m_info.inputSchema : nlohmann::json::object();
    }

    ToolResult execute(const nlohmann::json &args, const QString &workdir) override
    {
        Q_UNUSED(workdir); // MCP 服务器自管工作目录
        QMutexLocker locker(&m_mutex);
        return m_client->callTool(m_info.name, args);
    }

    // 上下文透明化：工具来自哪个 MCP 服务器
    QString serverName() const { return m_serverName; }

private:
    QString m_serverName;
    McpClient::ToolInfo m_info;
    std::shared_ptr<McpClient> m_client;
    QMutex m_mutex;
};

} // namespace lens::mcp
