#include <QtTest/QtTest>

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

    void fail(const QString &error)
    {
        if (m_callbacks.onError)
            m_callbacks.onError(error);
    }

    void finish() // 正常收流：真实传输在 reply finished 时调用
    {
        if (m_callbacks.onFinished)
            m_callbacks.onFinished();
    }

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

QTEST_MAIN(TestAgentSessionEvents)
#include "test_agent_session_events.moc"
