#pragma once

#include "lens/core/providers/ProtocolAdapter.hpp"

namespace lens::anthropic {

// Anthropic messages 协议适配器（POST {base}/v1/messages，x-api-key 鉴权）。
// 请求体：system 独立于 messages；assistant 的工具调用为 tool_use 块，
// 工具结果以 user 消息中的 tool_result 块回传（相邻 Tool 消息合并进同一 user 消息）。
// 无状态：块类型与分段下发的 usage 都记在传入的 ChatCompletionStream 上
class AnthropicAdapter : public ProtocolAdapter
{
public:
    QUrl resolveEndpoint(const QString &baseUrl) const override;
    QUrl resolveModelsEndpoint(const QString &baseUrl) const override;
    QList<QPair<QByteArray, QByteArray>> extraHeaders(const QString &apiKey) const override;
    bool isDoneEvent(const QByteArray &event) const override;
    chatcompletions::StreamDelta
    applyEvent(const nlohmann::json &payload,
               chatcompletions::ChatCompletionStream &stream) const override;
    QString errorFromEvent(const nlohmann::json &payload) const override;

protected:
    nlohmann::json doBuildRequestBody(const std::vector<Message> &history, const QString &model,
                                     const QString &systemPrompt, bool stream,
                                     const std::vector<ToolSpec> &tools,
                                     const RequestFeatures &features) const override;
};

} // namespace lens::anthropic
