#include "lens/core/providers/ProtocolAdapter.hpp"

#include "lens/core/providers/AnthropicAdapter.hpp"
#include "lens/core/providers/ChatCompletionsRequest.hpp"
#include "lens/core/providers/ResponsesAdapter.hpp"

namespace lens {
namespace {

// chat completions 适配器：薄封装现有 chatcompletions 实现
class ChatCompletionsAdapter : public ProtocolAdapter
{
public:
    QUrl resolveEndpoint(const QString &baseUrl) const override
    {
        return QUrl(detail::joinEndpoint(baseUrl, QStringLiteral("chat/completions")));
    }

    QUrl resolveModelsEndpoint(const QString &baseUrl) const override
    {
        return QUrl(detail::openAiModelsEndpoint(baseUrl, QStringLiteral("chat/completions")));
    }

    QList<QPair<QByteArray, QByteArray>> extraHeaders(const QString &apiKey) const override
    {
        return {{QByteArrayLiteral("Authorization"), "Bearer " + apiKey.toUtf8()}};
    }

    nlohmann::json doBuildRequestBody(const std::vector<Message> &history, const QString &model,
                                     const QString &systemPrompt, bool stream,
                                     const std::vector<ToolSpec> &tools,
                                     const RequestFeatures &features) const override
    {
        nlohmann::json body = chatcompletions::buildRequestBody(
            history, model, systemPrompt, stream, detail::toChatCompletionsTools(tools));
        if (features.serverSideSearch)
            body["web_search_options"] = nlohmann::json::object(); // OpenAI 服务端搜索
        if (features.maxOutputTokens > 0)
            body["max_tokens"] = features.maxOutputTokens;
        if (const QString effort = detail::openAiReasoningEffort(features.thinking);
            !effort.isEmpty())
            body["reasoning_effort"] = effort.toStdString();
        return body;
    }

    bool isDoneEvent(const QByteArray &event) const override
    {
        return event.trimmed() == QByteArrayLiteral("[DONE]");
    }

    chatcompletions::StreamDelta
    applyEvent(const nlohmann::json &payload,
               chatcompletions::ChatCompletionStream &stream) const override
    {
        return stream.apply(payload);
    }

    QString errorFromEvent(const nlohmann::json &payload) const override
    {
        if (payload.is_discarded() || !payload.is_object())
            return {};
        const auto it = payload.find("error");
        if (it == payload.end() || !it->is_object())
            return {};
        return detail::errorMessageOr(*it, QStringLiteral("chat completions 协议错误"));
    }
};

} // namespace

std::unique_ptr<ProtocolAdapter> makeProtocolAdapter(Protocol protocol)
{
    switch (protocol) {
    case Protocol::Responses:
        return std::make_unique<responses::ResponsesAdapter>();
    case Protocol::Anthropic:
        return std::make_unique<anthropic::AnthropicAdapter>();
    case Protocol::ChatCompletions:
        break;
    }
    return std::make_unique<ChatCompletionsAdapter>();
}

} // namespace lens
