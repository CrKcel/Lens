#pragma once

#include "lens/core/Conversation.hpp"
#include "lens/core/providers/ChatCompletionsClient.hpp"
#include "lens/core/providers/ITransport.hpp"
#include "lens/core/tools/BuiltinTool.hpp"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

namespace lens {

// 适配的 API 协议。覆盖 chat completions 系、OpenAI responses 与 Anthropic messages；
// 新协议在此枚举下扩展适配器，
// AgentSession 与工具调用循环保持协议无关。
enum class Protocol {
    ChatCompletions,
    Responses,
    Anthropic,
};

inline QString protocolToString(Protocol protocol)
{
    switch (protocol) {
    case Protocol::ChatCompletions: return QStringLiteral("chat_completions");
    case Protocol::Responses: return QStringLiteral("responses");
    case Protocol::Anthropic: return QStringLiteral("anthropic");
    }
    return QStringLiteral("chat_completions");
}

inline std::optional<Protocol> protocolFromString(const QString &name)
{
    if (name == QLatin1String("chat_completions")) return Protocol::ChatCompletions;
    if (name == QLatin1String("responses")) return Protocol::Responses;
    if (name == QLatin1String("anthropic")) return Protocol::Anthropic;
    return std::nullopt;
}

// 思考模式强度：Disabled 不发送思考参数；其余档位按协议映射
// （OpenAI 系 reasoning_effort，max 档取 "xhigh"；anthropic 走预算 token）
enum class ThinkingLevel {
    Disabled,
    Low,
    Medium,
    High,
    Max,
};

inline std::optional<ThinkingLevel> thinkingLevelFromString(const QString &name)
{
    if (name == QLatin1String("disabled")) return ThinkingLevel::Disabled;
    if (name == QLatin1String("low")) return ThinkingLevel::Low;
    if (name == QLatin1String("medium")) return ThinkingLevel::Medium;
    if (name == QLatin1String("high")) return ThinkingLevel::High;
    if (name == QLatin1String("max")) return ThinkingLevel::Max;
    return std::nullopt;
}

// 请求级附加能力：按供应商能力开启，由适配器转换为各协议的服务端机制
struct RequestFeatures {
    // 服务端联网搜索：模型侧直接联网检索，不经过本地工具往返。
    // 映射：chat completions → web_search_options；anthropic →
    // web_search_20250305 服务端工具；responses → {"type":"web_search"} 内建工具。
    // 开启时调用方不应再下发本地 web_search 工具（见 AgentSession 的过滤）。
    bool serverSideSearch = false;
    // 思考模式强度，映射见 ThinkingLevel 注释；Disabled 时请求体不携带思考参数
    ThinkingLevel thinking = ThinkingLevel::Disabled;
};

// 协议适配器：统一内部表示（Message / ToolSpec / 系统提示词）→ 各家请求体与鉴权头，
// 各家 SSE 事件 → 统一的 ChatCompletionStream 累积。无状态，可跨会话复用。
class ProtocolAdapter
{
public:
    virtual ~ProtocolAdapter() = default;

    // 完整请求 URL。baseUrl 允许两种形态：完整端点路径（原样使用，兼容老配置）
    // 或主机/根路径（由适配器补全自家 API 路径）。
    virtual QUrl resolveEndpoint(const QString &baseUrl) const = 0;

    // 模型清单端点（GET）。baseUrl 形态约定同 resolveEndpoint。
    virtual QUrl resolveModelsEndpoint(const QString &baseUrl) const = 0;

    // Content-Type 之外的请求头（鉴权、协议版本等）
    virtual QList<QPair<QByteArray, QByteArray>> extraHeaders(const QString &apiKey) const = 0;

    // 请求体。tools 为通用 ToolSpec，由适配器转换为各家格式；为空则不携带 tools。
    virtual nlohmann::json buildRequestBody(const std::vector<Message> &history,
                                            const QString &model,
                                            const QString &systemPrompt, bool stream,
                                            const std::vector<ToolSpec> &tools,
                                            const RequestFeatures &features = {}) const = 0;

    // 该 SSE data 负载是否表示流结束（如 chat completions 的 [DONE] 哨兵）
    virtual bool isDoneEvent(const QByteArray &event) const = 0;

    // 解析一个 SSE data 负载并累积进 stream，返回其中的新增增量
    virtual chatcompletions::StreamDelta
    applyEvent(const nlohmann::json &payload,
               chatcompletions::ChatCompletionStream &stream) const = 0;

    // 若该负载是协议级错误事件，返回错误描述；否则返回空
    virtual QString errorFromEvent(const nlohmann::json &payload) const = 0;
};

// 按协议构造适配器
std::unique_ptr<ProtocolAdapter> makeProtocolAdapter(Protocol protocol);

namespace detail {

inline QString trimTrailingSlashes(QString base)
{
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

// baseUrl 已含 path 时原样返回，否则拼接（用于兼容“完整端点”与“根路径”两种配置形态）
inline QString joinEndpoint(const QString &baseUrl, const QString &path)
{
    if (baseUrl.contains(path))
        return baseUrl;
    QString base = trimTrailingSlashes(baseUrl);
    QString suffix = path;
    while (suffix.startsWith(QLatin1Char('/')))
        suffix.remove(0, 1);
    return base + QLatin1Char('/') + suffix;
}

// OpenAI 系模型清单端点：baseUrl 已带自家 API 路径时把该段替换为 models，
// 否则在根路径上补 /models
inline QString openAiModelsEndpoint(const QString &baseUrl, const QString &apiPath)
{
    const QString base = trimTrailingSlashes(baseUrl);
    if (const qsizetype pos = base.indexOf(apiPath); pos >= 0)
        return base.left(pos) + QStringLiteral("models") + base.mid(pos + apiPath.size());
    return joinEndpoint(base, QStringLiteral("models"));
}

// OpenAI 系思考强度取值（reasoning_effort / reasoning.effort）；max 档取 "xhigh"，
// Disabled 返回空串（请求体省略参数）
inline QString openAiReasoningEffort(ThinkingLevel level)
{
    switch (level) {
    case ThinkingLevel::Low: return QStringLiteral("low");
    case ThinkingLevel::Medium: return QStringLiteral("medium");
    case ThinkingLevel::High: return QStringLiteral("high");
    case ThinkingLevel::Max: return QStringLiteral("xhigh");
    case ThinkingLevel::Disabled: break;
    }
    return {};
}

} // namespace detail

} // namespace lens
