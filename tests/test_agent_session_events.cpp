#include <QtTest/QtTest>

#include <QNetworkReply>
#include <QSignalSpy>

#include <lens/core/agent/AgentSession.hpp>
#include <lens/core/providers/ITransport.hpp>
#include <lens/core/tools/ToolRegistry.hpp>

using namespace lens;

namespace {

// 可编程的假传输：不起网络，按用例喂 SSE 分片或触发各回调。
// cancel() 复刻 QNetworkTransport 的收尾路径（abort → finished → onCanceled），
// 用于锁定"一次回合只通知一次 idle"的契约
class StubTransport final : public ITransport
{
public:
    void start(const HttpRequest &, StreamCallbacks callbacks) override
    {
        ++startCount;
        m_callbacks = std::move(callbacks);
    }

    void cancel() override
    {
        if (m_callbacks.onCanceled)
            m_callbacks.onCanceled();
    }

    void feed(const QByteArray &sse)
    {
        if (m_callbacks.onChunk)
            m_callbacks.onChunk(sse);
    }

    void fail(const TransportError &error)
    {
        if (m_callbacks.onError)
            m_callbacks.onError(error);
    }

    void fail(const QString &message, int httpStatus = 0, int retryAfterSeconds = 0)
    {
        fail(TransportError{message, httpStatus, 0, retryAfterSeconds});
    }

    void finish() // 正常收流：真实传输在 reply finished 时调用
    {
        if (m_callbacks.onFinished)
            m_callbacks.onFinished();
    }

    int startCount = 0; // 发起的请求数（重试会重发）

private:
    StreamCallbacks m_callbacks;
};

} // namespace

// AgentSession 的事件契约（用假传输，不需要服务器）：回合的每个终止路径
// （正常结束 / 传输错误 / 协议错误 / 用户取消）都只发一次 idle——
// 订阅方据此清 streaming 标志，重复通知会让状态机收到无对应回合的 idle
class TestAgentSessionEvents : public QObject
{
    Q_OBJECT

private slots:
    void protocolErrorEmitsSingleIdle();
    void transportErrorEmitsSingleIdle();
    void userCancelEmitsSingleIdle();
    void finishedEmitsSingleIdle();
    void retryableErrorRetriesThenSucceeds();
    void nonRetryableErrorFailsImmediately();
    void retriesExhaustedFailsOnce();
    void cancelDuringBackoffStopsRetry();
};

void TestAgentSessionEvents::protocolErrorEmitsSingleIdle()
{
    ToolRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    StubTransport *stub = transport.get();
    AgentSession session(std::move(transport), &registry);
    QSignalSpy failed(&session, &AgentSession::failed);
    QSignalSpy idle(&session, &AgentSession::idle);

    session.sendUserMessage(QStringLiteral("hi"));
    QVERIFY(session.busy());
    QCOMPARE(idle.count(), 0);

    // 协议级错误事件：中止本回合，随后 cancel() 触发传输层取消回调
    stub->feed(QByteArrayLiteral("data: {\"error\":{\"message\":\"boom\"}}\n\n"));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(idle.count(), 1);
    QVERIFY(!session.busy());
}

void TestAgentSessionEvents::transportErrorEmitsSingleIdle()
{
    ToolRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    StubTransport *stub = transport.get();
    AgentSession session(std::move(transport), &registry);
    QSignalSpy failed(&session, &AgentSession::failed);
    QSignalSpy idle(&session, &AgentSession::idle);

    session.sendUserMessage(QStringLiteral("hi"));
    stub->fail(QStringLiteral("连接中断"));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(idle.count(), 1);
    QVERIFY(!session.busy());
}

void TestAgentSessionEvents::userCancelEmitsSingleIdle()
{
    ToolRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    AgentSession session(std::move(transport), &registry);
    QSignalSpy idle(&session, &AgentSession::idle);

    session.sendUserMessage(QStringLiteral("hi"));
    session.cancel();
    QCOMPARE(idle.count(), 1);
    QVERIFY(!session.busy());

    session.cancel(); // 已空闲：不再重复通知
    QCOMPARE(idle.count(), 1);
}

void TestAgentSessionEvents::finishedEmitsSingleIdle()
{
    ToolRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    StubTransport *stub = transport.get();
    AgentSession session(std::move(transport), &registry);
    QSignalSpy completed(&session, &AgentSession::assistantCompleted);
    QSignalSpy idle(&session, &AgentSession::idle);

    session.sendUserMessage(QStringLiteral("hi"));
    stub->feed(QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\n"));
    stub->feed(QByteArrayLiteral("data: [DONE]\n\n"));
    stub->finish();

    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(0).value<Message>().content, QStringLiteral("ok"));
    QCOMPARE(idle.count(), 1); // 无工具调用：回合结束
    QVERIFY(!session.busy());
}

// 传输层错误自动重试（maxRetries>0 才启用）：可重试错误（HTTP 503 等）按退避重发，
// busy 保持、不发 failed/idle；鉴权等不可重试错误与次数耗尽仍走单次 idle 终止路径。
// 用例统一借 Retry-After 把退避压到 1 秒，避免长等待
void TestAgentSessionEvents::retryableErrorRetriesThenSucceeds()
{
    ToolRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    StubTransport *stub = transport.get();
    AgentSession session(std::move(transport), &registry);
    session.setMaxRetries(3);
    QSignalSpy failed(&session, &AgentSession::failed);
    QSignalSpy retryScheduled(&session, &AgentSession::retryScheduled);
    QSignalSpy completed(&session, &AgentSession::assistantCompleted);
    QSignalSpy idle(&session, &AgentSession::idle);

    session.sendUserMessage(QStringLiteral("hi"));
    QCOMPARE(stub->startCount, 1);
    stub->fail(QStringLiteral("过载"), 503, 1); // Retry-After: 1s
    QCOMPARE(retryScheduled.count(), 1);
    QCOMPARE(retryScheduled.first().at(0).toInt(), 1); // 第 1 次重试
    QCOMPARE(retryScheduled.first().at(1).toInt(), 3);
    QCOMPARE(retryScheduled.first().at(2).toInt(), 1000);
    QVERIFY(session.busy()); // 重试期间回合未终止
    QCOMPARE(failed.count(), 0);
    QCOMPARE(idle.count(), 0);

    QTest::qWait(1500); // 等退避计时器触发重发
    QCOMPARE(stub->startCount, 2);
    stub->feed(QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\n"));
    stub->feed(QByteArrayLiteral("data: [DONE]\n\n"));
    stub->finish();

    QCOMPARE(completed.count(), 1);
    QCOMPARE(idle.count(), 1);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!session.busy());
}

void TestAgentSessionEvents::nonRetryableErrorFailsImmediately()
{
    ToolRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    StubTransport *stub = transport.get();
    AgentSession session(std::move(transport), &registry);
    session.setMaxRetries(3);
    QSignalSpy failed(&session, &AgentSession::failed);
    QSignalSpy retryScheduled(&session, &AgentSession::retryScheduled);
    QSignalSpy idle(&session, &AgentSession::idle);

    session.sendUserMessage(QStringLiteral("hi"));
    stub->fail(QStringLiteral("鉴权失败"), 401); // 401 重试也不会好
    QCOMPARE(failed.count(), 1);
    QCOMPARE(idle.count(), 1);
    QCOMPARE(retryScheduled.count(), 0);
    QCOMPARE(stub->startCount, 1);
    QVERIFY(!session.busy());
}

void TestAgentSessionEvents::retriesExhaustedFailsOnce()
{
    ToolRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    StubTransport *stub = transport.get();
    AgentSession session(std::move(transport), &registry);
    session.setMaxRetries(1);
    QSignalSpy failed(&session, &AgentSession::failed);
    QSignalSpy retryScheduled(&session, &AgentSession::retryScheduled);
    QSignalSpy idle(&session, &AgentSession::idle);

    session.sendUserMessage(QStringLiteral("hi"));
    stub->fail(QStringLiteral("限流"), 429, 1);
    QCOMPARE(retryScheduled.count(), 1);
    QTest::qWait(1500); // 重发后再失败：次数已耗尽
    QCOMPARE(stub->startCount, 2);
    stub->fail(QStringLiteral("限流"), 429, 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(idle.count(), 1);
    QCOMPARE(retryScheduled.count(), 1); // 没有第三次
    QVERIFY(!session.busy());
}

void TestAgentSessionEvents::cancelDuringBackoffStopsRetry()
{
    ToolRegistry registry;
    auto transport = std::make_unique<StubTransport>();
    StubTransport *stub = transport.get();
    AgentSession session(std::move(transport), &registry);
    session.setMaxRetries(3);
    QSignalSpy failed(&session, &AgentSession::failed);
    QSignalSpy retryScheduled(&session, &AgentSession::retryScheduled);
    QSignalSpy idle(&session, &AgentSession::idle);

    session.sendUserMessage(QStringLiteral("hi"));
    // 模拟连接中途断开：无 HTTP 状态 + RemoteHostClosed
    stub->fail(TransportError{QStringLiteral("连接中断"), 0,
                              int(QNetworkReply::RemoteHostClosedError), 1});
    QCOMPARE(retryScheduled.count(), 1);
    session.cancel(); // 退避等待期间取消：待执行的定时器必须作废
    QCOMPARE(idle.count(), 1);
    QTest::qWait(1500);
    QCOMPARE(stub->startCount, 1); // 未重发
    QCOMPARE(failed.count(), 0);
    QCOMPARE(idle.count(), 1); // 没有重复通知
    QVERIFY(!session.busy());
}

QTEST_MAIN(TestAgentSessionEvents)
#include "test_agent_session_events.moc"
