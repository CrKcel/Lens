#pragma once

#include "lens/core/Conversation.hpp"

#include <QList>
#include <QMap>
#include <QString>
#include <nlohmann/json.hpp>
#include <vector>

namespace lens::chatcompletions {

// 构造 chat/completions 请求体。systemPrompt 为空时不插入 system 段；
// history 中的 System 消息被忽略——系统提示词统一由 PromptAssembler 编排。
// tools 为 function-calling 格式的工具数组（由适配器转换，
// 见 ProtocolAdapter 的 detail::toChatCompletionsTools），传空数组或 discarded
// 时不携带 tools 字段。
nlohmann::json buildRequestBody(const std::vector<Message> &history,
                                const QString &model,
                                const QString &systemPrompt,
                                bool stream,
                                const nlohmann::json &tools = nlohmann::json());

// 一次 apply 产生的增量：正文与思考过程分开交付
struct StreamDelta {
    QString content;
    QString reasoning;
};

// 累积一次回合的流式响应，组装完整的 assistant 消息（文本 + 思考 + 工具调用）。
// 既是 chat.completion.chunk 的累积器，也是其它协议适配器的统一落点：
// 各家 SSE 事件由 ProtocolAdapter 转成增量写入（appendContent / mergeToolCall 等）。
// 兼容 llama.cpp / OpenAI 的字段形态：
//  · delta.content: string 或 null（llama.cpp 首帧为 null）
//  · delta.reasoning_content: 思考模型的推理增量（Qwen3.5 等）
// 一个回合内复用同一实例，每回合重置。
class ChatCompletionStream
{
public:
    // 喂入一个 chunk 负载，返回其中新增的增量
    StreamDelta apply(const nlohmann::json &chunk);

    // —— 协议适配器共用的累积接口 ——
    void appendContent(const QString &text) { m_content += text; }
    void appendReasoning(const QString &text) { m_reasoning += text; }
    // 按 index 合并工具调用分片：id/name 非空时覆盖，arguments 追加
    void mergeToolCall(int index, const QString &id, const QString &name,
                       const QString &argumentsDelta);
    void setFinishReason(const QString &reason) { m_finishReason = reason; }
    void setUsage(const TokenUsage &usage) { m_usage = usage; }

    // 该 delta index 上是否已登记本地工具调用。协议适配器据此区分本地工具与
    // 服务端工具（如 anthropic 的 server_tool_use 块同样流式下发输入，但不是
    // 本地调用）——状态属于本回合的累积器，适配器自身保持无状态
    bool hasToolCall(int index) const { return m_toolCalls.contains(index); }

    QString content() const { return m_content; }
    QString reasoning() const { return m_reasoning; }
    QList<ToolCall> toolCalls() const; // 按 delta index 顺序
    QString finishReason() const { return m_finishReason; }
    const TokenUsage &usage() const { return m_usage; }
    bool isDone() const { return m_done; } // 流结束（[DONE] 或协议等价事件）

    void markDone() { m_done = true; }

private:
    QString m_content;
    QString m_reasoning;
    QMap<int, ToolCall> m_toolCalls;
    QString m_finishReason;
    TokenUsage m_usage;
    bool m_done = false;
};

} // namespace lens::chatcompletions
