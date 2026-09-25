#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
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

struct Message {
    Role role = Role::User;
    QString content;
    QDateTime createdAt;
    QList<ToolCall> toolCalls; // 仅 Assistant
    QString toolCallId;        // 仅 Tool：对应的调用 id
    QString reasoning;         // 仅 Assistant：思考过程（reasoning_content），不回传给 API
};

struct Conversation {
    qint64 id = 0;
    QString title;
    QString workdir; // 工作文件夹：Agent 读写与内置工具的根目录
    QDateTime createdAt;
    QDateTime updatedAt;
};

} // namespace lens
