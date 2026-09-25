#pragma once

#include "lens/core/mcp/McpClient.hpp"
#include "lens/core/tools/BuiltinTool.hpp"

#include <QMutex>
#include <memory>

namespace lens::mcp {

// 把一个 MCP 远程工具桥接成 IBuiltinTool：execute() 转发到共享的 McpClient。
// 同一服务器的工具共享一个客户端；工具循环串行执行，配置读取用互斥量保护。
class McpTool : public IBuiltinTool
{
public:
    McpTool(QString serverName, McpClient::ToolInfo info, std::shared_ptr<McpClient> client)
        : m_serverName(std::move(serverName))
        , m_info(std::move(info))
        , m_client(std::move(client))
    {
    }

    QString name() const override { return m_info.name; }
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
