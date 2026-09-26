#include "lens/core/providers/ResponsesClient.hpp"

namespace lens::responses {
namespace {

// history → input item 列表
nlohmann::json buildInputItems(const std::vector<Message> &history)
{
    nlohmann::json items = nlohmann::json::array();
    for (const Message &message : history) {
        if (message.role == Role::System)
            continue; // 系统提示词放 instructions
        switch (message.role) {
        case Role::User: {
            auto content = nlohmann::json::array();
            if (!message.content.isEmpty())
                content.push_back({{"type", "input_text"},
                                   {"text", message.content.toStdString()}});
            for (const ImageAttachment &image : message.images) {
                content.push_back({{"type", "input_image"},
                                   {"image_url", imageDataUrl(image).toStdString()}});
            }
            items.push_back({{"type", "message"}, {"role", "user"}, {"content", std::move(content)}});
            break;
        }
        case Role::Assistant:
            if (!message.content.isEmpty()) {
                items.push_back(
                    {{"type", "message"},
                     {"role", "assistant"},
                     {"content",
                      nlohmann::json::array({{{"type", "output_text"},
                                               {"text", message.content.toStdString()}}})}});
            }
            for (const ToolCall &call : message.toolCalls) {
                items.push_back({{"type", "function_call"},
                                 {"call_id", call.id.toStdString()},
                                 {"name", call.name.toStdString()},
                                 {"arguments", call.arguments.toStdString()}});
            }
            break;
        case Role::Tool: {
            items.push_back({{"type", "function_call_output"},
                             {"call_id", message.toolCallId.toStdString()},
                             {"output", message.content.toStdString()}});
            // function_call_output 只接受字符串输出：图片紧随其后合成一条 user 消息
            if (!message.images.isEmpty()) {
                auto content = nlohmann::json::array(
                    {{{"type", "input_text"}, {"text", "[工具返回的图片]"}}});
                for (const ImageAttachment &image : message.images) {
                    content.push_back({{"type", "input_image"},
                                       {"image_url", imageDataUrl(image).toStdString()}});
                }
                items.push_back(
                    {{"type", "message"}, {"role", "user"}, {"content", std::move(content)}});
            }
            break;
        }
        case Role::System:
            break;
        }
    }
    return items;
}

} // namespace

QUrl ResponsesAdapter::resolveEndpoint(const QString &baseUrl) const
{
    return QUrl(detail::joinEndpoint(baseUrl, QStringLiteral("responses")));
}

QUrl ResponsesAdapter::resolveModelsEndpoint(const QString &baseUrl) const
{
    return QUrl(detail::openAiModelsEndpoint(baseUrl, QStringLiteral("responses")));
}

QList<QPair<QByteArray, QByteArray>> ResponsesAdapter::extraHeaders(const QString &apiKey) const
{
    return {{QByteArrayLiteral("Authorization"), "Bearer " + apiKey.toUtf8()}};
}

nlohmann::json ResponsesAdapter::buildRequestBody(const std::vector<Message> &history,
                                                  const QString &model,
                                                  const QString &systemPrompt, bool stream,
                                                  const std::vector<ToolSpec> &tools,
                                                  const RequestFeatures &features) const
{
    nlohmann::json body = {{"model", model.toStdString()},
                           {"input", buildInputItems(history)},
                           {"stream", stream},
                           {"store", false}}; // 本地已有完整历史，不让服务端留存状态
    if (const QString effort = detail::openAiReasoningEffort(features.thinking);
        !effort.isEmpty())
        body["reasoning"] = {{"effort", effort.toStdString()}};
    if (!systemPrompt.isEmpty())
        body["instructions"] = systemPrompt.toStdString();
    if (!tools.empty() || features.serverSideSearch) {
        auto array = nlohmann::json::array();
        for (const ToolSpec &spec : tools) {
            array.push_back({{"type", "function"},
                             {"name", spec.name.toStdString()},
                             {"description", spec.description.toStdString()},
                             {"parameters", spec.parameters}});
        }
        if (features.serverSideSearch)
            array.push_back({{"type", "web_search"}}); // OpenAI 内建搜索工具
        body["tools"] = std::move(array);
    }
    return body;
}

bool ResponsesAdapter::isDoneEvent(const QByteArray &) const
{
    return false; // 无哨兵帧，response.completed 事件内标记完成
}

chatcompletions::StreamDelta
ResponsesAdapter::applyEvent(const nlohmann::json &payload,
                             chatcompletions::ChatCompletionStream &stream) const
{
    chatcompletions::StreamDelta delta;
    if (payload.is_discarded() || !payload.is_object())
        return delta;
    const std::string type = payload.value("type", std::string());

    if (type == "response.output_item.added") {
        const auto itemIt = payload.find("item");
        if (itemIt != payload.end() && itemIt->is_object()
            && itemIt->value("type", std::string()) == "function_call") {
            const int index = payload.value("output_index", 0);
            stream.mergeToolCall(index,
                                 QString::fromStdString(itemIt->value("call_id", std::string())),
                                 QString::fromStdString(itemIt->value("name", std::string())),
                                 QString());
        }
    } else if (type == "response.output_text.delta") {
        delta.content = QString::fromStdString(payload.value("delta", std::string()));
        stream.appendContent(delta.content);
    } else if (type == "response.reasoning_text.delta"
               || type == "response.reasoning_summary_text.delta") {
        delta.reasoning = QString::fromStdString(payload.value("delta", std::string()));
        stream.appendReasoning(delta.reasoning);
    } else if (type == "response.function_call_arguments.delta") {
        const int index = payload.value("output_index", 0);
        stream.mergeToolCall(index, QString(), QString(),
                             QString::fromStdString(payload.value("delta", std::string())));
    } else if (type == "response.completed" || type == "response.incomplete") {
        stream.setFinishReason(type == "response.completed" ? "stop" : "incomplete");
        const auto responseIt = payload.find("response");
        if (responseIt != payload.end() && responseIt->is_object()) {
            const auto usageIt = responseIt->find("usage");
            if (usageIt != responseIt->end() && usageIt->is_object()) {
                TokenUsage usage;
                usage.valid = true;
                if (auto it = usageIt->find("input_tokens");
                    it != usageIt->end() && it->is_number())
                    usage.promptTokens = it->get<qint64>();
                if (auto it = usageIt->find("output_tokens");
                    it != usageIt->end() && it->is_number())
                    usage.completionTokens = it->get<qint64>();
                const auto detailsIt = usageIt->find("input_tokens_details");
                if (detailsIt != usageIt->end() && detailsIt->is_object()) {
                    if (auto it = detailsIt->find("cached_tokens");
                        it != detailsIt->end() && it->is_number())
                        usage.cachedTokens = it->get<qint64>();
                }
                stream.setUsage(usage);
            }
        }
        stream.markDone();
    }
    return delta;
}

QString ResponsesAdapter::errorFromEvent(const nlohmann::json &payload) const
{
    if (payload.is_discarded() || !payload.is_object())
        return {};
    const std::string type = payload.value("type", std::string());
    if (type != "response.failed" && type != "error")
        return {};
    const auto it = payload.find(type == "error" ? "error" : "response");
    if (it != payload.end() && it->is_object()) {
        const auto errorIt = it->find("error");
        if (errorIt != it->end() && errorIt->is_object())
            return QString::fromStdString(
                errorIt->value("message", std::string("responses 协议错误")));
        return QString::fromStdString(it->value("message", std::string("responses 协议错误")));
    }
    // {"type":"error","message":"..."}：错误信息直接在顶层
    const auto messageIt = payload.find("message");
    if (messageIt != payload.end() && messageIt->is_string())
        return QString::fromStdString(messageIt->get<std::string>());
    return QStringLiteral("responses 协议错误");
}

} // namespace lens::responses
