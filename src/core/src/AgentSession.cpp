#include "lens/core/agent/AgentSession.hpp"

#include <QMetaObject>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

namespace lens {

AgentSession::AgentSession(std::unique_ptr<ITransport> transport, ToolRegistry *registry,
                           QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_registry(registry)
{
}

void AgentSession::setRequestConfig(const QString &endpointUrl, const QString &apiKey,
                                    const QString &model)
{
    m_endpoint = endpointUrl;
    m_apiKey = apiKey;
    m_model = model;
}

void AgentSession::sendUserMessage(const QString &text)
{
    if (m_busy || text.trimmed().isEmpty())
        return;
    Message message;
    message.role = Role::User;
    message.content = text;
    message.createdAt = QDateTime::currentDateTimeUtc();
    m_history.push_back(message);

    m_toolCallsThisTurn = 0;
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
    request.url = QUrl(m_endpoint);
    request.headers = {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")},
                       {QByteArrayLiteral("Authorization"),
                        "Bearer " + m_apiKey.toUtf8()}};
    request.body = QByteArray::fromStdString(chatcompletions::buildRequestBody(
        m_history, m_model, m_systemPrompt, true,
        m_registry->toChatCompletionsTools()).dump());

    m_transport->start(request,
                       {[this](const QByteArray &bytes) {
                            m_sse.feed(bytes, [this](const QByteArray &event) {
                                if (event == QByteArrayLiteral("[DONE]")) {
                                    m_stream.markDone();
                                    return;
                                }
                                const auto payload = nlohmann::json::parse(
                                    event.constData(), event.constData() + event.size(),
                                    nullptr, false);
                                const auto delta = m_stream.apply(payload);
                                if (!delta.content.isEmpty())
                                    emit assistantDelta(delta.content);
                                if (!delta.reasoning.isEmpty())
                                    emit reasoningDelta(delta.reasoning);
                            });
                        },
                        [this] { finishAssistantMessage(); },
                        [this](QString error) {
                            m_busy = false;
                            emit failed(error);
                            emit idle();
                        },
                        [this] { // 用户取消
                            m_busy = false;
                            emit idle();
                        }});
}

void AgentSession::finishAssistantMessage()
{
    Message assistant;
    assistant.role = Role::Assistant;
    assistant.content = m_stream.content();
    assistant.reasoning = m_stream.reasoning();
    assistant.toolCalls = m_stream.toolCalls();
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
                emit toolCallFinished(call.id, result.output);
                Message toolMessage;
                toolMessage.role = Role::Tool;
                toolMessage.content = result.output;
                toolMessage.toolCallId = call.id;
                toolMessage.createdAt = QDateTime::currentDateTimeUtc();
                m_history.push_back(toolMessage);
                processNextToolCall();
            },
            Qt::QueuedConnection);
    });
}

} // namespace lens
