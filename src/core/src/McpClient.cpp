#include "lens/core/mcp/McpClient.hpp"

#include <QDateTime>
#include <QProcess>
#include <QThread>
#include <algorithm>

namespace lens::mcp {

using nlohmann::json;

namespace {

inline std::string jsonToString(const json &value) { return value.dump(); }

} // namespace

McpClient::McpClient(ServerConfig config)
    : m_config(std::move(config))
{
}

McpClient::~McpClient()
{
    stop();
}

bool McpClient::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void McpClient::stop()
{
    if (!m_process)
        return;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    m_process.reset();
}

bool McpClient::start(QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (m_config.command.trimmed().isEmpty())
        return fail(QStringLiteral("MCP 服务器 %1 未配置启动命令").arg(m_config.name));

    stop();
    m_process = std::make_unique<QProcess>();
    m_process->setProgram(m_config.command);
    m_process->setArguments(m_config.args);
    m_process->start();
    if (!m_process->waitForStarted(5000))
        return fail(QStringLiteral("MCP 服务器 %1 启动失败：%2")
                        .arg(m_config.name, m_process->errorString()));

    const json handshake = request(QStringLiteral("initialize"),
                                   json{{"protocolVersion", "2024-11-05"},
                                        {"capabilities", json::object()},
                                        {"clientInfo",
                                         {{"name", "lens"}, {"version", "1.0"}}}},
                                   15000, error);
    if (handshake.is_discarded()) {
        stop(); // 握手失败不遗留子进程
        return false;
    }
    // initialized 通知：无 id，服务器不回帧
    QString notifyError;
    sendLine(QByteArray::fromStdString(
                 jsonToString(json{{"jsonrpc", "2.0"},
                                   {"method", "notifications/initialized"}})),
             &notifyError);
    if (!notifyError.isEmpty()) {
        stop();
        return fail(notifyError);
    }
    return true;
}

void McpClient::sendLine(const QByteArray &line, QString *error)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        if (error)
            *error = QStringLiteral("MCP 服务器 %1 未运行").arg(m_config.name);
        return;
    }
    m_process->write(line);
    m_process->write("\n");
    if (!m_process->waitForBytesWritten(5000) && error)
        *error = QStringLiteral("MCP 服务器 %1 写入超时").arg(m_config.name);
}

bool McpClient::waitReadable(int timeoutMs)
{
    if (!m_process)
        return false;
    return m_process->bytesAvailable() > 0
        || m_process->waitForReadyRead(qMin(timeoutMs, 100));
}

void McpClient::pumpIncoming()
{
    if (!m_process)
        return;
    m_buffer += m_process->readAll();
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        const json frame = json::parse(line.constData(), line.constData() + line.size(), nullptr,
                                       false);
        if (frame.is_discarded() || !frame.is_object())
            continue;
        const auto idIt = frame.find("id");
        if (idIt != frame.end() && idIt->is_number_integer())
            m_responses.insert(idIt->get<int>(), frame);
        else
            m_notifications.append(QString::fromStdString(jsonToString(frame)));
    }
}

bool McpClient::waitForResponse(int id, json *result, QString *error, int timeoutMs)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    for (;;) {
        pumpIncoming();
        const auto it = m_responses.constFind(id);
        if (it != m_responses.constEnd()) {
            const json frame = *it;
            m_responses.erase(it);
            if (frame.contains("error")) {
                const auto &errorJson = frame.at("error");
                return fail(QString::fromStdString(
                    errorJson.value("message", std::string("MCP 请求失败"))));
            }
            *result = frame.value("result", json::object());
            return true;
        }
        if (QDateTime::currentMSecsSinceEpoch() >= deadline)
            return fail(QStringLiteral("MCP 服务器 %1 响应超时").arg(m_config.name));
        if (!m_process || m_process->state() != QProcess::Running)
            return fail(QStringLiteral("MCP 服务器 %1 已退出").arg(m_config.name));
        if (!waitReadable(int(deadline - QDateTime::currentMSecsSinceEpoch())))
            QThread::msleep(10);
    }
}

json McpClient::request(const QString &method, const json &params, int timeoutMs, QString *error)
{
    const int id = m_nextId++;
    QString sendError;
    sendLine(QByteArray::fromStdString(jsonToString(
                 json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method.toStdString()},
                      {"params", params}})),
             &sendError);
    if (!sendError.isEmpty()) {
        if (error)
            *error = sendError;
        return json();
    }
    json result;
    if (!waitForResponse(id, &result, error, timeoutMs))
        return json();
    return result;
}

QList<McpClient::ToolInfo> McpClient::listTools(QString *error)
{
    const json result = request(QStringLiteral("tools/list"), json::object(), 15000, error);
    QList<ToolInfo> tools;
    if (result.is_discarded() || !result.contains("tools") || !result.at("tools").is_array())
        return tools;
    for (const auto &entry : result.at("tools")) {
        if (!entry.is_object() || !entry.contains("name") || !entry.at("name").is_string())
            continue;
        ToolInfo info;
        info.name = QString::fromStdString(entry.at("name").get<std::string>());
        if (entry.contains("description") && entry.at("description").is_string())
            info.description =
                QString::fromStdString(entry.at("description").get<std::string>());
        if (entry.contains("inputSchema") && entry.at("inputSchema").is_object())
            info.inputSchema = entry.at("inputSchema");
        tools.append(std::move(info));
    }
    return tools;
}

ToolResult McpClient::callTool(const QString &toolName, const json &arguments, int timeoutMs)
{
    const json result = request(QStringLiteral("tools/call"),
                                json{{"name", toolName.toStdString()},
                                     {"arguments", arguments.is_object() ? arguments
                                                                          : json::object()}},
                                timeoutMs, nullptr);
    if (result.is_discarded() || !result.is_object())
        return {false, QStringLiteral("MCP 工具 %1 调用失败").arg(toolName)};

    QStringList texts;
    if (result.contains("content") && result.at("content").is_array()) {
        for (const auto &block : result.at("content")) {
            if (block.is_object() && block.value("type", std::string()) == "text"
                && block.contains("text") && block.at("text").is_string())
                texts.append(QString::fromStdString(block.at("text").get<std::string>()));
        }
    }
    const bool isError = result.value("isError", false);
    return {!isError, texts.isEmpty() ? QStringLiteral("(无输出)") : texts.join(QStringLiteral("\n"))};
}

} // namespace lens::mcp
