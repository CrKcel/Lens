#include "lens/core/providers/ChatCompletionsRequest.hpp"

namespace lens::chatcompletions {
namespace {

nlohmann::json imageParts(const QList<ImageAttachment> &images)
{
    auto parts = nlohmann::json::array();
    for (const ImageAttachment &image : images) {
        parts.push_back({{"type", "image_url"},
                         {"image_url", {{"url", imageDataUrl(image).toStdString()}}}});
    }
    return parts;
}

// 多模态 content 数组：文本非空时带 text part，后接图片 parts
nlohmann::json multimodalContent(const QString &text, const QList<ImageAttachment> &images)
{
    auto parts = nlohmann::json::array();
    if (!text.isEmpty())
        parts.push_back({{"type", "text"}, {"text", text.toStdString()}});
    for (auto &part : imageParts(images))
        parts.push_back(std::move(part));
    return parts;
}

nlohmann::json messageToJson(const Message &message)
{
    // 文本文件附件格式化进正文（协议无关形态，见 formatTextAttachments）
    const QString text = formatTextAttachments(message.content, message.files);
    nlohmann::json j = {{"role", roleToString(message.role).toStdString()},
                        {"content", text.toStdString()}};
    if (message.role == Role::User && (!message.images.isEmpty() || !message.files.isEmpty()))
        j["content"] = multimodalContent(text, message.images);
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
        // OpenAI 系 tool 角色的 content 只接受字符串：工具返回的图片
        // 紧随其后合成一条 user 消息携带，兼容性最好
        if (message.role == Role::Tool && !message.images.isEmpty()) {
            messages.push_back({{"role", "user"},
                                {"content", multimodalContent(QStringLiteral("[工具返回的图片]"),
                                                              message.images)}});
        }
    }
    nlohmann::json body = {{"model", model.toStdString()},
                           {"messages", std::move(messages)},
                           {"stream", stream}};
    if (tools.is_array() && !tools.empty())
        body["tools"] = tools;
    return body;
}

} // namespace lens::chatcompletions
