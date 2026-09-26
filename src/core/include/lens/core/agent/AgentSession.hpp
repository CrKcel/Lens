#pragma once

#include "lens/core/Conversation.hpp"
#include "lens/core/providers/ChatCompletionsClient.hpp"
#include "lens/core/providers/ITransport.hpp"
#include "lens/core/providers/ProtocolAdapter.hpp"
#include "lens/core/providers/SseParser.hpp"
#include "lens/core/tools/ToolRegistry.hpp"

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

namespace lens {

// 一个会话的 Agent 执行体：用户消息 → chat completions 流式请求 →
// 若模型请求工具则执行并把结果回灌，循环直到模型给出最终回复。
// 运行细节：SSE 在主线程增量解析（QNetworkTransport 回调），
// 工具执行放到 QThreadPool，结果经事件循环回灌，GUI 全程不阻塞。
class AgentSession : public QObject
{
    Q_OBJECT

public:
    AgentSession(std::unique_ptr<ITransport> transport, ToolRegistry *registry,
                 QObject *parent = nullptr);

    void setRequestConfig(const QString &endpointUrl, const QString &apiKey,
                          const QString &model);
    void setProtocol(Protocol protocol); // 缺省 chat completions，需在发起请求前设置
    void setServerSideSearch(bool enabled) { m_serverSideSearch = enabled; }
    void setThinkingLevel(ThinkingLevel level) { m_thinkingLevel = level; }
    void setSystemPrompt(const QString &systemPrompt) { m_systemPrompt = systemPrompt; }
    void setWorkdir(const QString &workdir) { m_workdir = workdir; }
    void setHistory(std::vector<Message> history) { m_history = std::move(history); }
    const std::vector<Message> &history() const { return m_history; }
    bool busy() const { return m_busy; }

public slots:
    void sendUserMessage(const QString &text) { sendUserMessage(text, {}); }
    void sendUserMessage(const QString &text, const QList<ImageAttachment> &images)
    {
        sendUserMessage(text, images, {});
    }
    void sendUserMessage(const QString &text, const QList<ImageAttachment> &images,
                         const QList<TextAttachment> &files);
    void cancel();

signals:
    void assistantDelta(const QString &text);
    void reasoningDelta(const QString &text); // 思考过程增量（reasoning_content）
    void assistantCompleted(const lens::Message &message);
    void toolCallStarted(const QString &id, const QString &name, const QString &args);
    void toolCallFinished(const QString &id, const QString &output,
                          const QList<ImageAttachment> &images = {});
    void failed(const QString &message);
    void idle(); 

private:
    void startTurn();
    void finishAssistantMessage();
    void processNextToolCall();

    std::unique_ptr<ITransport> m_transport;
    ToolRegistry *m_registry;
    std::unique_ptr<ProtocolAdapter> m_adapter;
    SseParser m_sse;
    chatcompletions::ChatCompletionStream m_stream;
    std::vector<Message> m_history;
    QList<ToolCall> m_pendingToolCalls;

    QString m_endpoint;
    QString m_apiKey;
    QString m_model;
    Protocol m_protocol = Protocol::ChatCompletions;
    bool m_serverSideSearch = false;
    ThinkingLevel m_thinkingLevel = ThinkingLevel::Disabled;
    QString m_systemPrompt;
    QString m_workdir;
    bool m_busy = false;
    quint64 m_generation = 0; 
    int m_toolCallsThisTurn = 0;
    static constexpr int kMaxToolCallsPerTurn = 32;
};

} // namespace lens
