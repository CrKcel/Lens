#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <nlohmann/json.hpp>
#include <optional>

namespace lens {

enum class Role {
    System,
    User,
    Assistant,
    Tool,
};

inline QString roleToString(Role role)
{
    switch (role) {
    case Role::System: return QStringLiteral("system");
    case Role::User: return QStringLiteral("user");
    case Role::Assistant: return QStringLiteral("assistant");
    case Role::Tool: return QStringLiteral("tool");
    }
    return QStringLiteral("user");
}

inline std::optional<Role> roleFromString(const QString &text)
{
    if (text == QLatin1String("system")) return Role::System;
    if (text == QLatin1String("user")) return Role::User;
    if (text == QLatin1String("assistant")) return Role::Assistant;
    if (text == QLatin1String("tool")) return Role::Tool;
    return std::nullopt;
}

// assistant 消息携带的一次工具调用；arguments 为模型给出的 JSON 文本
struct ToolCall {
    QString id;
    QString name;
    QString arguments;
};

// 一次 assistant 回合服务端上报的用量；不同协议字段名不同，此处为归一结果
struct TokenUsage {
    qint64 promptTokens = 0;     // 输入（即发送给服务端的上下文长度）
    qint64 completionTokens = 0; // 输出
    qint64 cachedTokens = 0;     // 缓存命中部分（计入 promptTokens）
    bool valid = false;          // 服务端是否上报
};

inline nlohmann::json usageToJson(const TokenUsage &usage)
{
    if (!usage.valid) return nlohmann::json();
    return {
        {"prompt_tokens", usage.promptTokens},
        {"completion_tokens", usage.completionTokens},
        {"cached_tokens", usage.cachedTokens},
    };
}

inline TokenUsage usageFromJson(const nlohmann::json &json)
{
    TokenUsage usage;
    if (!json.is_object()) return usage;
    usage.valid = true;
    if (auto it = json.find("prompt_tokens"); it != json.end() && it->is_number())
        usage.promptTokens = it->get<qint64>();
    if (auto it = json.find("completion_tokens"); it != json.end() && it->is_number())
        usage.completionTokens = it->get<qint64>();
    if (auto it = json.find("cached_tokens"); it != json.end() && it->is_number())
        usage.cachedTokens = it->get<qint64>();
    return usage;
}

struct Message {
    Role role = Role::User;
    QString content;
    QDateTime createdAt;
    QList<ToolCall> toolCalls; // 仅 Assistant
    QString toolCallId;        // 仅 Tool：对应的调用 id
    QString reasoning;         // 仅 Assistant：思考过程（reasoning_content），不回传给 API
    TokenUsage usage;          // 仅 Assistant：服务端用量上报，未上报时 valid=false
};

struct Conversation {
    qint64 id = 0;
    QString title;
    QString workdir; // 工作文件夹：Agent 读写与内置工具的根目录
    QDateTime createdAt;
    QDateTime updatedAt;
};

} // namespace lens
