#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "AppSettings.hpp"
#include "ChatEngine.hpp"
#include "ChatSession.hpp"
#include "MessageListModel.hpp"
#include "lens/core/providers/ITransport.hpp"
#include "lens/core/storage/SessionStore.hpp"

using namespace lens;

namespace {

// 可编程的假传输（与 test_agent_session_events 同一模式）：不起网络，
// 捕获请求体供断言，按用例喂 SSE 分片
class StubTransport final : public ITransport
{
public:
    void start(const HttpRequest &request, StreamCallbacks callbacks) override
    {
        m_request = request;
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

    void finish()
    {
        if (m_callbacks.onFinished)
            m_callbacks.onFinished();
    }

    HttpRequest request() const { return m_request; }
    QString model() const
    {
        return QJsonDocument::fromJson(m_request.body)
            .object()
            .value(QStringLiteral("model"))
            .toString();
    }

private:
    HttpRequest m_request;
    StreamCallbacks m_callbacks;
};

} // namespace

// 多会话并行生成（ChatEngine + ChatSession，假传输，不需要服务器）：
// 会话注册表、并行流式互不干扰、重绑窗口不打断后台生成、删除收尾、
// 会话级模型覆盖进请求体、流式状态广播
class TestChatEngine : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void parallelSessionsStreamIndependently();
    void rebindingWindowKeepsBackgroundGeneration();
    void deleteConversationStopsAndCloses();
    void modelOverrideDrivesRequest();
    void streamingFlagBroadcast();

private:
    // 工厂产出的传输按创建次序索引（每会话一个传输实例，同会话多次
    // send 复用同一传输：start() 覆盖上一次请求）
    StubTransport *stub(int index) const { return m_stubs.at(index); }
    static void feedReply(StubTransport *stub, const QString &text);

    // 每个用例独立的临时目录（QTemporaryDir 析构时删除数据库文件）
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<SessionStore> m_store;
    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<ChatEngine> m_engine;
    QVector<StubTransport *> m_stubs;
};

void TestChatEngine::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_store = std::make_unique<SessionStore>(m_dir->filePath(QStringLiteral("sessions.db")));
    QVERIFY(m_store->open());
    m_settings = std::make_unique<AppSettings>(m_dir->filePath(QStringLiteral("settings.json")));
    m_settings->setEndpoint(QStringLiteral("http://127.0.0.1:9/v1/chat/completions"));
    m_settings->setApiKey(QStringLiteral("k"));
    m_settings->setModel(QStringLiteral("global-model"));
    m_engine = std::make_unique<ChatEngine>(m_store.get(), m_settings.get(), m_dir->path());
    m_engine->setTransportFactory([this] {
        auto stub = std::make_unique<StubTransport>();
        m_stubs.append(stub.get());
        return stub;
    });
    m_stubs.clear();
}

void TestChatEngine::feedReply(StubTransport *stub, const QString &text)
{
    stub->feed(QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"") + text.toUtf8()
               + QByteArrayLiteral("\"}}]}\n\n"));
    stub->feed(QByteArrayLiteral("data: [DONE]\n\n"));
    stub->finish();
}

void TestChatEngine::parallelSessionsStreamIndependently()
{
    ChatSession *first = m_engine->createConversation(QStringLiteral("/tmp/a"));
    ChatSession *second = m_engine->createConversation(QStringLiteral("/tmp/b"));
    QVERIFY(first != second);
    QVERIFY(!m_engine->anyStreaming());

    first->send(QStringLiteral("hi-a"), {}, QStringLiteral("disabled"));
    QVERIFY(first->streaming());
    QVERIFY(m_engine->anyStreaming());
    // 第二个会话在第一个生成中照常发送：并行互不阻塞
    second->send(QStringLiteral("hi-b"), {}, QStringLiteral("disabled"));
    QVERIFY(second->streaming());

    feedReply(stub(0), QStringLiteral("reply-a"));
    QVERIFY(!first->streaming());
    QVERIFY(second->streaming()); // 另一会话不受影响

    feedReply(stub(1), QStringLiteral("reply-b"));
    QVERIFY(!second->streaming());
    QVERIFY(!m_engine->anyStreaming());

    // 各会话用自己的显示模型与持久化历史
    QCOMPARE(first->messages()->rowCount(), 2); // user + assistant
    QCOMPARE(second->messages()->rowCount(), 2);
    QCOMPARE(m_store->messages(first->conversationId()).size(), 2);
    QCOMPARE(m_store->messages(second->conversationId()).size(), 2);
    QVERIFY(first->conversationId() != second->conversationId());
}

void TestChatEngine::rebindingWindowKeepsBackgroundGeneration()
{
    ChatSession *first = m_engine->createConversation(QStringLiteral("/tmp/a"));
    first->send(QStringLiteral("hi"), {}, QStringLiteral("disabled"));
    QVERIFY(first->streaming());

    // "窗口切到另一个会话"：打开别的会话（登记新会话）不得中断第一个的回合
    ChatSession *second = m_engine->sessionFor(
        m_store->createConversation(QStringLiteral("other"), QStringLiteral("/tmp/b")));
    QVERIFY(second != first);
    QVERIFY(first->streaming());
    // 重绑回第一个会话（sessionFor 返回同一实例，现场仍在）
    QCOMPARE(m_engine->sessionFor(first->conversationId()), first);
    // 直接复现 facade 的重绑语义：再次 loadConversation 是幂等的，不重置流式行
    first->loadConversation(first->conversationId());
    QVERIFY(first->streaming());

    feedReply(stub(0), QStringLiteral("late-reply"));
    QVERIFY(!first->streaming());
    const QList<Message> history = m_store->messages(first->conversationId());
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.last().content, QStringLiteral("late-reply"));
}

void TestChatEngine::deleteConversationStopsAndCloses()
{
    ChatSession *session = m_engine->createConversation(QStringLiteral("/tmp/a"));
    const qint64 id = session->conversationId();
    session->send(QStringLiteral("hi"), {}, QStringLiteral("disabled"));
    QVERIFY(m_engine->anyStreaming());

    QSignalSpy closed(m_engine.get(), &ChatEngine::sessionClosed);
    m_engine->deleteConversation(id);
    QCOMPARE(closed.count(), 1);
    QCOMPARE(closed.first().at(0).toLongLong(), id);
    QVERIFY(!m_engine->anyStreaming());
    QVERIFY(!m_engine->sessionFor(id)); // 注册表摘除，store 里也已删除
    for (const Conversation &conversation : m_store->conversations())
        QVERIFY2(conversation.id != id, "被删会话不应残留在 store");
}

void TestChatEngine::modelOverrideDrivesRequest()
{
    ChatSession *session = m_engine->createConversation(QStringLiteral("/tmp/a"));
    StubTransport *transport = stub(0); // 同会话复用同一传输实例

    // 未覆盖：请求体用全局激活模型
    session->send(QStringLiteral("hi"), {}, QStringLiteral("disabled"));
    QCOMPARE(transport->model(), QStringLiteral("global-model"));
    feedReply(transport, QStringLiteral("ok"));

    // 覆盖后：请求体用覆盖模型（同供应商不同模型）
    session->setModelOverride(0, QStringLiteral("override-model"));
    session->send(QStringLiteral("again"), {}, QStringLiteral("disabled"));
    QCOMPARE(transport->model(), QStringLiteral("override-model"));
    feedReply(transport, QStringLiteral("ok"));
    QVERIFY(session->hasModelOverride());

    // 清除覆盖：回到全局模型
    session->clearModelOverride();
    QVERIFY(!session->hasModelOverride());
    session->send(QStringLiteral("third"), {}, QStringLiteral("disabled"));
    QCOMPARE(transport->model(), QStringLiteral("global-model"));
    feedReply(transport, QStringLiteral("ok"));
}

void TestChatEngine::streamingFlagBroadcast()
{
    ChatSession *session = m_engine->createConversation(QStringLiteral("/tmp/a"));
    QSignalSpy broadcast(m_engine.get(), &ChatEngine::sessionStreamingChanged);

    const qint64 id = session->conversationId();
    session->send(QStringLiteral("hi"), {}, QStringLiteral("disabled"));
    QCOMPARE(broadcast.count(), 1);
    QCOMPARE(broadcast.first().at(0).toLongLong(), id);
    QCOMPARE(broadcast.first().at(1).toBool(), true);

    feedReply(stub(0), QStringLiteral("ok"));
    QCOMPARE(broadcast.count(), 2);
    QCOMPARE(broadcast.last().at(0).toLongLong(), id);
    QCOMPARE(broadcast.last().at(1).toBool(), false);
}

QTEST_MAIN(TestChatEngine)
#include "test_chat_engine.moc"
