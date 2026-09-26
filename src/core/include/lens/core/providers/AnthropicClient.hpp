#pragma once

#include "lens/core/providers/ProtocolAdapter.hpp"

#include <QMap>

namespace lens::anthropic {

// Anthropic messages 协议适配器（POST {base}/v1/messages，x-api-key 鉴权）。
// 请求体：system 独立于 messages；assistant 的工具调用为 tool_use 块，
// 工具结果以 user 消息中的 tool_result 块回传（相邻 Tool 消息合并进同一 user 消息）。
class AnthropicAdapter : public ProtocolAdapter
{
public:
    QUrl resolveEndpoint(const QString &baseUrl) const override;
    QList<QPair<QByteArray, QByteArray>> extraHeaders(const QString &apiKey) const override;
    nlohmann::json buildRequestBody(const std::vector<Message> &history, const QString &model,
                                    const QString &systemPrompt, bool stream,
                                    const std::vector<ToolSpec> &tools,
                                    const RequestFeatures &features = {}) const override;
    bool isDoneEvent(const QByteArray &event) const override;
    chatcompletions::StreamDelta
    applyEvent(const nlohmann::json &payload,
               chatcompletions::ChatCompletionStream &stream) const override;
    QString errorFromEvent(const nlohmann::json &payload) const override;

private:
    // index → 块类型。服务端工具（如 web_search_20250305）的 server_tool_use 块
    // 同样用 input_json_delta 流式下发输入，但不是本地工具调用，须按块类型区分。
    // 适配器被 AgentSession 单实例复用，状态在每回合的 message_start 清零。
    mutable QMap<int, QString> m_blockTypes;
    // usage 拆在 message_start（输入）与 message_delta（输出）两个事件里，
    // 先暂存输入侧，message_delta 到达时合并成完整 TokenUsage
    mutable qint64 m_pendingPromptTokens = 0;
    mutable qint64 m_pendingCachedTokens = 0;
};

} // namespace lens::anthropic
