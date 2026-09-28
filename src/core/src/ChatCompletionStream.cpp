#include "lens/core/providers/ChatCompletionStream.hpp"

namespace lens::chatcompletions {
namespace {

// 解析 chat completions 形态的 usage 对象（prompt/completion_tokens + cached 明细）。
// llama.cpp 末帧把 usage 放在 chunk 顶层，OpenAI 的 include_usage 末帧无 choices。
// 仅本翻译单元使用，保持内部链接避免污染命名空间
void parseUsage(const nlohmann::json &usageJson, ChatCompletionStream &stream)
{
    if (!usageJson.is_object() || !usageJson.contains("prompt_tokens")
        || !usageJson.contains("completion_tokens"))
        return;
    TokenUsage usage;
    usage.valid = true;
    if (usageJson.at("prompt_tokens").is_number())
        usage.promptTokens = usageJson.at("prompt_tokens").get<qint64>();
    if (usageJson.at("completion_tokens").is_number())
        usage.completionTokens = usageJson.at("completion_tokens").get<qint64>();
    if (usageJson.contains("prompt_tokens_details")
        && usageJson.at("prompt_tokens_details").is_object()
        && usageJson.at("prompt_tokens_details").contains("cached_tokens")
        && usageJson.at("prompt_tokens_details").at("cached_tokens").is_number())
        usage.cachedTokens =
            usageJson.at("prompt_tokens_details").at("cached_tokens").get<qint64>();
    stream.setUsage(usage);
}

} // namespace

StreamDelta ChatCompletionStream::apply(const nlohmann::json &chunk)
{
    StreamDelta delta;
    if (chunk.is_discarded() || !chunk.is_object())
        return delta;
    // usage 可能在任意 chunk 顶层（惯例为最后一帧，stream_options.include_usage 时该帧无 choices）
    if (chunk.contains("usage"))
        parseUsage(chunk.at("usage"), *this);
    if (!chunk.contains("choices"))
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
