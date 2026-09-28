#pragma once

#include "lens/core/Conversation.hpp"
#include "lens/core/providers/ChatCompletionStream.hpp"
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
    void setMaxOutputTokens(int tokens) { m_maxOutputTokens = tokens; }
    void setImagesEnabled(bool enabled) { m_imagesEnabled = enabled; }
    void setMaxRetries(int retries) { m_maxRetries = retries; }
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
    void retryScheduled(int attempt, int maxRetries, int delayMs); // 传输层错误自动重试
    void idle();

private:
    void startTurn();
    void finishAssistantMessage();
    void processNextToolCall();
    bool retryable(const TransportError &error) const;
    int retryDelayMs(const TransportError &error) const;

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
    int m_maxOutputTokens = 0;    // 模型最大输出 token，0 未配置（请求体不带上限）
    bool m_imagesEnabled = true;  // 模型图片输入开关，关闭时请求体剥离图片附件
    QString m_systemPrompt;
    QString m_workdir;
    bool m_busy = false;
    quint64 m_generation = 0; 
    int m_toolCallsThisTurn = 0;
    int m_maxRetries = 0;   // 传输层错误自动重试次数，0 关闭
    int m_retryAttempt = 0; // 当前回合已重试次数
    static constexpr int kMaxToolCallsPerTurn = 32;
};

} // namespace lens
