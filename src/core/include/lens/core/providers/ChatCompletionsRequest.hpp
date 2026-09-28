#pragma once

#include "lens/core/Conversation.hpp"

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

} // namespace lens::chatcompletions
