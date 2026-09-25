#pragma once

#include "lens/core/tools/BuiltinTool.hpp"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <memory>

class QProcess;

namespace lens::mcp {

// 一个 stdio MCP 服务器的连接配置
struct ServerConfig {
    QString name;
    QString command;
    QStringList args;
};

// stdio MCP 客户端：QProcess 启动服务器进程，JSON-RPC 2.0 通信（换行分隔帧）。
// 同步阻塞实现：设计为在工具执行线程（QThreadPool）内串行调用，不要在主线程使用。
class McpClient
{
public:
    explicit McpClient(ServerConfig config);
    ~McpClient();

    McpClient(const McpClient &) = delete;
    McpClient &operator=(const McpClient &) = delete;

    // 启动子进程并完成 initialize 握手；失败返回 false 并写 error
    bool start(QString *error = nullptr);
    void stop();
    bool isRunning() const;

    struct ToolInfo {
        QString name;
        QString description;
        nlohmann::json inputSchema;
    };

    QList<ToolInfo> listTools(QString *error = nullptr);

    // 调用远程工具；content 中的文本块拼接为输出，isError 决定 ToolResult::ok
    ToolResult callTool(const QString &toolName, const nlohmann::json &arguments,
                        int timeoutMs = 60000);

private:
    // 发送一次 JSON-RPC 请求并等待响应（id 匹配），返回 result 字段
    nlohmann::json request(const QString &method, const nlohmann::json &params, int timeoutMs,
                           QString *error);
    bool waitForResponse(int id, nlohmann::json *result, QString *error, int timeoutMs);
    void pumpIncoming(); // 读取 stdin 缓冲，解析帧存入 m_responses
    void sendLine(const QByteArray &line, QString *error);
    bool waitReadable(int timeoutMs);

    ServerConfig m_config;
    std::unique_ptr<QProcess> m_process;
    QByteArray m_buffer;
    QMap<int, nlohmann::json> m_responses;      // id → 完整响应帧
    QStringList m_notifications;                // 服务器主动通知（暂存，忽略内容）
    int m_nextId = 1;
};

} // namespace lens::mcp
