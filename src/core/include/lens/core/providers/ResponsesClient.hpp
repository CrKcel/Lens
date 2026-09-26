#pragma once

#include "lens/core/providers/ProtocolAdapter.hpp"

namespace lens::responses {

// OpenAI responses 协议适配器（POST {base}/responses，Bearer 鉴权）。
// 请求体：系统提示词放 instructions，输入为 item 列表——工具调用是 function_call item，
// 工具结果以 function_call_output item 回传。
class ResponsesAdapter : public ProtocolAdapter
{
public:
    QUrl resolveEndpoint(const QString &baseUrl) const override;
    QUrl resolveModelsEndpoint(const QString &baseUrl) const override;
    QList<QPair<QByteArray, QByteArray>> extraHeaders(const QString &apiKey) const override;
    nlohmann::json buildRequestBody(const std::vector<Message> &history, const QString &model,
                                    const QString &systemPrompt, bool stream,
                                    const std::vector<ToolSpec> &tools,
                                    const RequestFeatures &features = {}) const override;
    bool isDoneEvent(const QByteArray &) const override;
    chatcompletions::StreamDelta
    applyEvent(const nlohmann::json &payload,
               chatcompletions::ChatCompletionStream &stream) const override;
    QString errorFromEvent(const nlohmann::json &payload) const override;
};

} // namespace lens::responses
