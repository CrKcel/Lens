#include "lens/core/agent/AgentSession.hpp"

#include <QMetaObject>
#include <QNetworkReply>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

namespace lens {

AgentSession::AgentSession(std::unique_ptr<ITransport> transport, ToolRegistry *registry,
                           QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_registry(registry)
    , m_adapter(makeProtocolAdapter(Protocol::ChatCompletions))
{
}

void AgentSession::setRequestConfig(const QString &endpointUrl, const QString &apiKey,
                                    const QString &model)
{
    m_endpoint = endpointUrl;
    m_apiKey = apiKey;
    m_model = model;
}

void AgentSession::setProtocol(Protocol protocol)
{
    if (m_protocol == protocol)
        return;
    m_protocol = protocol;
    m_adapter = makeProtocolAdapter(protocol);
}

void AgentSession::sendUserMessage(const QString &text, const QList<ImageAttachment> &images,
                                   const QList<TextAttachment> &files)
{
    if (m_busy || text.trimmed().isEmpty())
        return;
    Message message;
    message.role = Role::User;
    message.content = text;
    message.images = images;
    message.files = files;
    message.createdAt = QDateTime::currentDateTimeUtc();
    m_history.push_back(message);

    m_toolCallsThisTurn = 0;
    m_retryAttempt = 0;
    m_busy = true;
    ++m_generation;
    startTurn();
}

void AgentSession::cancel()
{
    if (!m_busy)
        return;
    ++m_generation;
    m_pendingToolCalls.clear();
    m_busy = false;
    m_transport->cancel();
    emit idle();
}

void AgentSession::startTurn()
{
    m_stream = chatcompletions::ChatCompletionStream{};
    m_sse = SseParser{};

    HttpRequest request;
    request.url = m_adapter->resolveEndpoint(m_endpoint);
    request.headers = {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")}};
    request.headers += m_adapter->extraHeaders(m_apiKey);

    RequestFeatures features;
    features.serverSideSearch = m_serverSideSearch;
    features.thinking = m_thinkingLevel;
    features.maxOutputTokens = m_maxOutputTokens;
    features.images = m_imagesEnabled;
    std::vector<ToolSpec> specs = m_registry->specs();
    request.body = QByteArray::fromStdString(
        m_adapter->buildRequestBody(m_history, m_model, m_systemPrompt, true, specs, features)
            .dump());

    const quint64 generation = m_generation; // 取消/协议错误会使其失效，过期回调直接丢弃
    m_transport->start(request,
                       {[this, generation](const QByteArray &bytes) {
                            if (generation != m_generation)
                                return;
                            m_sse.feed(bytes, [this, generation](const QByteArray &event) {
                                if (generation != m_generation)
                                    return;
                                if (m_adapter->isDoneEvent(event))
                                    return; // 流结束帧（[DONE] 等）不是 JSON，直接跳过
                                const auto payload = nlohmann::json::parse(
                                    event.constData(), event.constData() + event.size(),
                                    nullptr, false);
                                if (!payload.is_discarded()) {
                                    // 协议级错误事件：中止本回合，与传输层错误同路径处理
                                    const QString protocolError =
                                        m_adapter->errorFromEvent(payload);
                                    if (!protocolError.isEmpty()) {
                                        ++m_generation;
                                        m_busy = false;
                                        emit failed(protocolError);
                                        emit idle();
                                        m_transport->cancel();
                                        return;
                                    }
                                }
                                const auto delta = m_adapter->applyEvent(payload, m_stream);
                                if (!delta.content.isEmpty())
                                    emit assistantDelta(delta.content);
                                if (!delta.reasoning.isEmpty())
                                    emit reasoningDelta(delta.reasoning);
                            });
                        },
                        [this, generation] { // 完成回调同样带代际：取消后不再入历史
                            if (generation != m_generation)
                                return;
                            finishAssistantMessage();
                        },
                        [this, generation](const TransportError &error) {
                            // 传输层错误：可重试的（断连、繁忙）按退避自动重发，
                            // busy 保持、不发 idle；不可重试或次数耗尽才终止回合
                            if (retryable(error) && m_retryAttempt < m_maxRetries) {
                                ++m_retryAttempt;
                                const int delay = retryDelayMs(error);
                                emit retryScheduled(m_retryAttempt, m_maxRetries, delay);
                                QTimer::singleShot(delay, this, [this, generation] {
                                    if (generation != m_generation) // 退避期间已取消
                                        return;
                                    startTurn();
                                });
                                return;
                            }
                            m_busy = false;
                            emit failed(error.message);
                            emit idle();
                        },
                        [this] { // 取消：由发起方（cancel / 协议错误路径）自行 emit idle，
                                 // 传输层回调不再发第二次（否则订阅方收到重复的 idle）
                            m_busy = false;
                        }});
}

void AgentSession::finishAssistantMessage()
{
    Message assistant;
    assistant.role = Role::Assistant;
    assistant.content = m_stream.content();
    assistant.reasoning = m_stream.reasoning();
    assistant.toolCalls = m_stream.toolCalls();
    assistant.usage = m_stream.usage();
    assistant.createdAt = QDateTime::currentDateTimeUtc();
    m_history.push_back(assistant);
    emit assistantCompleted(assistant);

    if (assistant.toolCalls.isEmpty()) {
        m_busy = false;
        emit idle();
        return;
    }
    m_pendingToolCalls = assistant.toolCalls;
    processNextToolCall();
}

void AgentSession::processNextToolCall()
{
    if (m_pendingToolCalls.isEmpty()) {
        startTurn(); // 工具结果已入历史，发起下一轮模型请求（busy 保持）
        return;
    }

    const ToolCall call = m_pendingToolCalls.takeFirst();
    const quint64 generation = m_generation;
    const QString workdir = m_workdir;
    emit toolCallStarted(call.id, call.name, call.arguments);

    if (++m_toolCallsThisTurn > kMaxToolCallsPerTurn) {
        // 失控保护：补齐 tool 消息以保持协议一致（assistant.tool_calls 必须有结果），
        // 然后终止回合；下一条用户消息时模型可基于已有信息继续
        const QString limitNote = QStringLiteral(
            "工具调用次数已达上限（%1），本轮终止。请基于已有信息作答。")
            .arg(kMaxToolCallsPerTurn);
        Message toolMessage;
        toolMessage.role = Role::Tool;
        toolMessage.content = limitNote;
        toolMessage.toolCallId = call.id;
        m_history.push_back(toolMessage);
        emit toolCallFinished(call.id, limitNote);
        m_busy = false;
        emit failed(QStringLiteral("工具调用次数超过上限，回合已终止"));
        emit idle();
        return;
    }

    QThreadPool::globalInstance()->start([this, generation, call, workdir] {
        const auto args = nlohmann::json::parse(call.arguments.toStdString(), nullptr, false);
        const ToolResult result =
            args.is_discarded()
                ? ToolResult{false,
                             QStringLiteral("工具参数不是合法 JSON：%1").arg(call.arguments)}
                : m_registry->execute(call.name, args, workdir);

        QMetaObject::invokeMethod(
            this,
            [this, generation, call, result] {
                if (generation != m_generation) // 已取消，丢弃过期结果
                    return;
                emit toolCallFinished(call.id, result.output, result.images);
                Message toolMessage;
                toolMessage.role = Role::Tool;
                toolMessage.content = result.output;
                toolMessage.toolCallId = call.id;
                toolMessage.images = result.images;
                toolMessage.createdAt = QDateTime::currentDateTimeUtc();
                m_history.push_back(toolMessage);
                processNextToolCall();
            },
            Qt::QueuedConnection);
    });
}

// 可自动重试的传输层错误：供应商繁忙（限流/过载/超时）或连接中途断开。
// 鉴权失败（401/403）、参数错误（400）、DNS 解析失败、SSL 握手失败等重试也不会好，不在此列。
bool AgentSession::retryable(const TransportError &error) const
{
    switch (error.httpStatus) {
    case 408: // 请求超时
    case 429: // 限流
    case 500:
    case 502:
    case 503:
    case 504:
        return true;
    case 0: // 请求没到服务器：按网络错误码判断
        switch (static_cast<QNetworkReply::NetworkError>(error.networkError)) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::ProxyConnectionRefusedError:
        case QNetworkReply::ProxyConnectionClosedError:
        case QNetworkReply::ProxyTimeoutError:
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

// 重试退避：供应商的 Retry-After 优先（封顶 1 分钟，防溢出与异常长的等待），
// 否则指数退避 2s→4s→8s→16s→30s 封顶
int AgentSession::retryDelayMs(const TransportError &error) const
{
    if (error.retryAfterSeconds > 0)
        return qMin(error.retryAfterSeconds, 60) * 1000;
    const int backoffMs = 2000 << qMin(m_retryAttempt - 1, 4);
    return qMin(backoffMs, 30000);
}

} // namespace lens
