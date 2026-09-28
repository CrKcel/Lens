#pragma once

#include "lens/core/Conversation.hpp"

#include <QList>
#include <QMap>
#include <QString>
#include <nlohmann/json.hpp>

namespace lens::chatcompletions {

// 一次 apply 产生的增量：正文与思考过程分开交付
struct StreamDelta {
    QString content;
    QString reasoning;
};

// 累积一次回合的流式响应，组装完整的 assistant 消息（文本 + 思考 + 工具调用）。
// 协议中立的统一落点：chat.completion.chunk 直接喂入，其它协议的 SSE 事件由
// 各自的 ProtocolAdapter 转成增量写入（appendContent / mergeToolCall 等）。
// chat.completion.chunk 形态兼容 llama.cpp / OpenAI 的字段差异：
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
    void setUsage(const TokenUsage &usage) { m_usage = usage; }

    // 该 delta index 上是否已登记本地工具调用。协议适配器据此区分本地工具与
    // 服务端工具（如 anthropic 的 server_tool_use 块同样流式下发输入，但不是
    // 本地调用）——状态属于本回合的累积器，适配器自身保持无状态
    bool hasToolCall(int index) const { return m_toolCalls.contains(index); }

    QString content() const { return m_content; }
    QString reasoning() const { return m_reasoning; }
    QList<ToolCall> toolCalls() const; // 按 delta index 顺序
    const TokenUsage &usage() const { return m_usage; }

private:
    QString m_content;
    QString m_reasoning;
    QMap<int, ToolCall> m_toolCalls;
    TokenUsage m_usage;
};

} // namespace lens::chatcompletions
