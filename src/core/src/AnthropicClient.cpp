#include "lens/core/providers/AnthropicClient.hpp"

namespace lens::anthropic {
namespace {

nlohmann::json parseArgumentsOrEmpty(const QString &arguments)
{
    const auto parsed = nlohmann::json::parse(arguments.toStdString(), nullptr, false);
    return parsed.is_discarded() || !parsed.is_object() ? nlohmann::json::object() : parsed;
}

// history → messages 数组：Tool 结果合并进相邻的同一 user 消息（tool_result 块）
nlohmann::json buildMessages(const std::vector<Message> &history)
{
    nlohmann::json messages = nlohmann::json::array();
    nlohmann::json pendingToolResults = nlohmann::json::array();
    auto flushToolResults = [&] {
        if (pendingToolResults.empty())
            return;
        messages.push_back({{"role", "user"}, {"content", std::move(pendingToolResults)}});
        pendingToolResults = nlohmann::json::array();
    };

    for (const Message &message : history) {
        if (message.role == Role::System)
            continue; // 系统提示词放在顶层 system 字段
        if (message.role == Role::Tool) {
            pendingToolResults.push_back({{"type", "tool_result"},
                                          {"tool_use_id", message.toolCallId.toStdString()},
                                          {"content", message.content.toStdString()}});
            continue;
        }
        flushToolResults();

        nlohmann::json blocks = nlohmann::json::array();
        // 思考模式（扩展思考）要求把 assistant 的 thinking 块回传。签名由服务端下发，
        // 兼容实现普遍接受空签名；官方 API 的签名校验暂未支持。
        if (message.role == Role::Assistant && !message.reasoning.isEmpty()) {
            blocks.push_back({{"type", "thinking"},
                              {"thinking", message.reasoning.toStdString()},
                              {"signature", ""}});
        }
        if (!message.content.isEmpty())
            blocks.push_back({{"type", "text"}, {"text", message.content.toStdString()}});
        if (message.role == Role::Assistant) {
            for (const ToolCall &call : message.toolCalls) {
                blocks.push_back({{"type", "tool_use"},
                                  {"id", call.id.toStdString()},
                                  {"name", call.name.toStdString()},
                                  {"input", parseArgumentsOrEmpty(call.arguments)}});
            }
        }
        if (blocks.empty())
            blocks.push_back({{"type", "text"}, {"text", ""}});
        messages.push_back({{"role", message.role == Role::Assistant ? "assistant" : "user"},
                            {"content", std::move(blocks)}});
    }
    flushToolResults();
    return messages;
}

} // namespace

QUrl AnthropicAdapter::resolveEndpoint(const QString &baseUrl) const
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.contains(QStringLiteral("v1/messages")))
        return QUrl(base); // 完整端点原样使用
    if (base.endsWith(QStringLiteral("/v1")))
        return QUrl(base + QStringLiteral("/messages"));
    return QUrl(base + QStringLiteral("/v1/messages"));
}

QList<QPair<QByteArray, QByteArray>> AnthropicAdapter::extraHeaders(const QString &apiKey) const
{
    return {{QByteArrayLiteral("x-api-key"), apiKey.toUtf8()},
            {QByteArrayLiteral("anthropic-version"), QByteArrayLiteral("2023-06-01")}};
}

nlohmann::json AnthropicAdapter::buildRequestBody(const std::vector<Message> &history,
                                                  const QString &model,
                                                  const QString &systemPrompt, bool stream,
                                                  const std::vector<ToolSpec> &tools,
                                                  const RequestFeatures &features) const
{
    nlohmann::json body = {{"model", model.toStdString()},
                           {"max_tokens", 8192}, // Anthropic 必填字段，取保守上限
                           {"messages", buildMessages(history)},
                           {"stream", stream}};
    if (!systemPrompt.isEmpty())
        body["system"] = systemPrompt.toStdString();
    if (!tools.empty() || features.serverSideSearch) {
        auto array = nlohmann::json::array();
        for (const ToolSpec &spec : tools) {
            array.push_back({{"name", spec.name.toStdString()},
                             {"description", spec.description.toStdString()},
                             {"input_schema", spec.parameters}});
        }
        if (features.serverSideSearch) {
            // 服务端搜索工具：搜索在 Anthropic 侧执行，结果经 tool_use/tool_result 之外的
            // server_tool_use / web_search_tool_result 块回传，无需本地处理
            array.push_back({{"type", "web_search_20250305"}, {"name", "web_search"}});
        }
        body["tools"] = std::move(array);
    }
    return body;
}

bool AnthropicAdapter::isDoneEvent(const QByteArray &) const
{
    return false; // 无哨兵帧，message_stop 事件内标记完成
}

chatcompletions::StreamDelta
AnthropicAdapter::applyEvent(const nlohmann::json &payload,
                             chatcompletions::ChatCompletionStream &stream) const
{
    chatcompletions::StreamDelta delta;
    if (payload.is_discarded() || !payload.is_object())
        return delta;
    const std::string type = payload.value("type", std::string());

    if (type == "message_start") {
        m_blockTypes.clear(); // 新回合：清空上一条的块类型记录
        m_pendingPromptTokens = 0;
        m_pendingCachedTokens = 0;
        // 输入侧用量随 message_start 下发，先暂存等 message_delta 的输出侧合并
        const auto messageIt = payload.find("message");
        if (messageIt != payload.end() && messageIt->is_object()) {
            const auto usageIt = messageIt->find("usage");
            if (usageIt != messageIt->end() && usageIt->is_object()) {
                if (auto it = usageIt->find("input_tokens");
                    it != usageIt->end() && it->is_number())
                    m_pendingPromptTokens = it->get<qint64>();
                if (auto it = usageIt->find("cache_read_input_tokens");
                    it != usageIt->end() && it->is_number())
                    m_pendingCachedTokens = it->get<qint64>();
            }
        }
    }

    if (type == "content_block_start") {
        const auto blockIt = payload.find("content_block");
        if (blockIt != payload.end() && blockIt->is_object()) {
            const int index = payload.value("index", 0);
            const QString blockType =
                QString::fromStdString(blockIt->value("type", std::string()));
            m_blockTypes[index] = blockType;
            if (blockType == QStringLiteral("tool_use")) {
                stream.mergeToolCall(index,
                                     QString::fromStdString(blockIt->value("id", std::string())),
                                     QString::fromStdString(blockIt->value("name", std::string())),
                                     QString());
            }
        }
    } else if (type == "content_block_delta") {
        const auto deltaIt = payload.find("delta");
        if (deltaIt == payload.end() || !deltaIt->is_object())
            return delta;
        const int index = payload.value("index", 0);
        const std::string deltaType = deltaIt->value("type", std::string());
        if (deltaType == "text_delta") {
            delta.content = QString::fromStdString(deltaIt->value("text", std::string()));
            stream.appendContent(delta.content);
        } else if (deltaType == "thinking_delta") {
            delta.reasoning = QString::fromStdString(deltaIt->value("thinking", std::string()));
            stream.appendReasoning(delta.reasoning);
        } else if (deltaType == "input_json_delta") {
            // 只有本地工具调用（tool_use 块）的参数增量才累积；
            // server_tool_use 等服务端块的输入由服务端自行消费
            if (m_blockTypes.value(index) == QLatin1String("tool_use"))
                stream.mergeToolCall(index, QString(), QString(),
                                     QString::fromStdString(
                                         deltaIt->value("partial_json", std::string())));
        }
    } else if (type == "message_delta") {
        const auto deltaIt = payload.find("delta");
        if (deltaIt != payload.end() && deltaIt->is_object()) {
            const QString stopReason =
                QString::fromStdString(deltaIt->value("stop_reason", std::string()));
            if (!stopReason.isEmpty()) {
                // Anthropic 的 tool_use 对应 chat completions 的 tool_calls
                stream.setFinishReason(stopReason == QStringLiteral("tool_use")
                                           ? QStringLiteral("tool_calls")
                                           : stopReason);
            }
        }
        // 输出侧用量与 message_start 暂存的输入侧合并
        const auto usageIt = payload.find("usage");
        if (usageIt != payload.end() && usageIt->is_object()) {
            TokenUsage usage;
            usage.valid = true;
            usage.promptTokens = m_pendingPromptTokens;
            usage.cachedTokens = m_pendingCachedTokens;
            if (auto it = usageIt->find("output_tokens");
                it != usageIt->end() && it->is_number())
                usage.completionTokens = it->get<qint64>();
            stream.setUsage(usage);
        }
    } else if (type == "message_stop") {
        stream.markDone();
    }
    return delta;
}

QString AnthropicAdapter::errorFromEvent(const nlohmann::json &payload) const
{
    if (payload.is_discarded() || !payload.is_object()
        || payload.value("type", std::string()) != "error")
        return {};
    const auto it = payload.find("error");
    if (it == payload.end() || !it->is_object())
        return QStringLiteral("Anthropic 协议错误");
    return QString::fromStdString(it->value("message", std::string("Anthropic 协议错误")));
}

} // namespace lens::anthropic
