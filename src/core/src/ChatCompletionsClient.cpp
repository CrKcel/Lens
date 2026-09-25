#include "lens/core/providers/ChatCompletionsClient.hpp"

namespace lens::chatcompletions {
namespace {

nlohmann::json messageToJson(const Message &message)
{
    nlohmann::json j = {{"role", roleToString(message.role).toStdString()},
                        {"content", message.content.toStdString()}};
    if (message.role == Role::Assistant && !message.toolCalls.isEmpty()) {
        auto calls = nlohmann::json::array();
        for (const ToolCall &call : message.toolCalls) {
            calls.push_back({{"id", call.id.toStdString()},
                             {"type", "function"},
                             {"function",
                              {{"name", call.name.toStdString()},
                               {"arguments", call.arguments.toStdString()}}}});
        }
        j["tool_calls"] = std::move(calls);
    }
    if (message.role == Role::Tool && !message.toolCallId.isEmpty())
        j["tool_call_id"] = message.toolCallId.toStdString();
    return j;
}

} // namespace

nlohmann::json buildRequestBody(const std::vector<Message> &history,
                                const QString &model,
                                const QString &systemPrompt,
                                bool stream,
                                const nlohmann::json &tools)
{
    nlohmann::json messages = nlohmann::json::array();
    if (!systemPrompt.isEmpty())
        messages.push_back({{"role", "system"}, {"content", systemPrompt.toStdString()}});
    for (const auto &message : history) {
        if (message.role == Role::System)
            continue;
        messages.push_back(messageToJson(message));
    }
    nlohmann::json body = {{"model", model.toStdString()},
                           {"messages", std::move(messages)},
                           {"stream", stream}};
    if (tools.is_array() && !tools.empty())
        body["tools"] = tools;
    return body;
}

QString extractDeltaText(const nlohmann::json &payload)
{
    if (payload.is_discarded() || !payload.contains("choices"))
        return {};
    const auto &choices = payload.at("choices");
    if (!choices.is_array() || choices.empty())
        return {};
    const auto &first = choices.front();
    if (!first.is_object() || !first.contains("delta"))
        return {};
    const auto &delta = first.at("delta");
    if (!delta.is_object() || !delta.contains("content"))
        return {};
    const auto &content = delta.at("content");
    if (content.is_string())
        return QString::fromStdString(content.get<std::string>());
    if (content.is_null())
        return {};
    return QString::fromStdString(content.dump());
}

chatcompletions::StreamDelta ChatCompletionStream::apply(const nlohmann::json &chunk)
{
    StreamDelta delta;
    if (chunk.is_discarded() || !chunk.contains("choices"))
        return delta;
    const auto &choices = chunk.at("choices");
    if (!choices.is_array() || choices.empty())
        return delta;
    const auto &choice = choices.front();
    if (!choice.is_object())
        return delta;

    if (choice.contains("delta") && choice.at("delta").is_object()) {
        const auto &deltaJson = choice.at("delta");
        if (deltaJson.contains("content") && deltaJson.at("content").is_string()) {
            delta.content = QString::fromStdString(deltaJson.at("content").get<std::string>());
            m_content += delta.content;
        }
        // 思考模型的推理增量（llama.cpp / DeepSeek 风格 reasoning_content）
        if (deltaJson.contains("reasoning_content")
            && deltaJson.at("reasoning_content").is_string()) {
            delta.reasoning =
                QString::fromStdString(deltaJson.at("reasoning_content").get<std::string>());
            m_reasoning += delta.reasoning;
        }
        if (deltaJson.contains("reasoning") && deltaJson.at("reasoning").is_string()) {
            // OpenRouter 等使用 reasoning 字段名的变体
            const QString reasoningAlt =
                QString::fromStdString(deltaJson.at("reasoning").get<std::string>());
            delta.reasoning += reasoningAlt;
            m_reasoning += reasoningAlt;
        }
        if (deltaJson.contains("tool_calls") && deltaJson.at("tool_calls").is_array()) {
            for (const auto &fragment : deltaJson.at("tool_calls")) {
                if (!fragment.is_object() || !fragment.contains("index")
                    || !fragment.at("index").is_number_integer())
                    continue;
                const int index = fragment.at("index").get<int>();
                QString id, name, arguments;
                if (fragment.contains("id") && fragment.at("id").is_string())
                    id = QString::fromStdString(fragment.at("id").get<std::string>());
                if (fragment.contains("function") && fragment.at("function").is_object()) {
                    const auto &function = fragment.at("function");
                    if (function.contains("name") && function.at("name").is_string())
                        name = QString::fromStdString(function.at("name").get<std::string>());
                    if (function.contains("arguments") && function.at("arguments").is_string())
                        arguments =
                            QString::fromStdString(function.at("arguments").get<std::string>());
                }
                mergeToolCall(index, id, name, arguments);
            }
        }
    }
    if (choice.contains("finish_reason") && choice.at("finish_reason").is_string())
        m_finishReason = QString::fromStdString(choice.at("finish_reason").get<std::string>());
    return delta;
}

void ChatCompletionStream::mergeToolCall(int index, const QString &id, const QString &name,
                                         const QString &argumentsDelta)
{
    ToolCall &call = m_toolCalls[index]; // 按 index 累积分片
    if (!id.isEmpty())
        call.id = id;
    if (!name.isEmpty())
        call.name = name;
    call.arguments += argumentsDelta;
}

QList<ToolCall> ChatCompletionStream::toolCalls() const
{
    QList<ToolCall> result;
    for (auto it = m_toolCalls.constBegin(); it != m_toolCalls.constEnd(); ++it)
        result.append(it.value());
    return result;
}

} // namespace lens::chatcompletions
