#include <QtTest/QtTest>

#include <QEventLoop>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <lens/core/agent/AgentSession.hpp>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/storage/SessionStore.hpp>
#include <lens/core/tools/ToolRegistry.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WebSearchTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

using namespace lens;

namespace {

// 本地 mock chat/completions 服务：
// 第 1 个请求返回 write(note.txt) 工具调用流；第 2 个起返回普通文本流。
class MockChatServer : public QObject
{
    Q_OBJECT

public:
    bool start()
    {
        connect(&m_server, &QTcpServer::newConnection, this,
                &MockChatServer::handleConnection);
        return m_server.listen(QHostAddress::LocalHost);
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/v1/chat/completions")
                        .arg(m_server.serverPort()));
    }

    int requestCount = 0;
    QList<QByteArray> bodies;
    // 请求 1 下发的工具调用（默认 write note.txt，多模态用例改为 read 图片）；
    // toolArgs 是模型给出的参数文本，引号需按 SSE JSON 字符串转义
    QByteArray toolName = "write";
    QByteArray toolArgs = "{\\\"path\\\":\\\"note.txt\\\",\\\"content\\\":\\\"written by mock\\\"}";

private slots:
    void handleConnection()
    {
        auto *socket = m_server.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            m_buffers[socket] += socket->readAll();
            tryRespond(socket);
        });
    }

    void tryRespond(QTcpSocket *socket)
    {
        const QByteArray &raw = m_buffers[socket];
        const int headerEnd = raw.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = raw.left(headerEnd).toLower();
        const int lengthKey = header.indexOf("content-length:");
        if (lengthKey < 0)
            return;
        const int bodyLength = QByteArrayView(header).mid(lengthKey + 15)
                                   .trimmed().toInt();
        const QByteArray body = raw.mid(headerEnd + 4);
        if (body.size() < bodyLength)
            return;
        m_buffers.remove(socket);
        bodies.append(body);
        respond(socket, ++requestCount);
    }

    void respond(QTcpSocket *socket, int requestNumber)
    {
        QByteArray sse;
        if (requestNumber == 1) {
            // 请求模型调用工具，参数分两个分片下发
            sse += "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"\"}}]}\n\n";
            sse += "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_abc\","
                   "\"type\":\"function\",\"function\":{\"name\":\"";
            sse += toolName;
            sse += "\",\"arguments\":\"\"}}]}}]}\n\n";
            sse += "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":"
                   "{\"arguments\":\"";
            sse += toolArgs;
            sse += "\"}}]}}]}\n\n";
            sse += "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n";
            sse += "data: {\"usage\":{\"prompt_tokens\":110,\"completion_tokens\":25,"
                   "\"total_tokens\":135,\"prompt_tokens_details\":{\"cached_tokens\":60}}}\n\n";
            sse += "data: [DONE]\n\n";
        } else {
            sse += "data: {\"choices\":[{\"delta\":{\"content\":\"文件已写入\"}}]}\n\n";
            sse += "data: {\"choices\":[{\"delta\":{\"content\":\"，任务完成\"}}]}\n\n";
            sse += "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n";
            sse += "data: {\"usage\":{\"prompt_tokens\":150,\"completion_tokens\":12,"
                   "\"total_tokens\":162}}\n\n";
            sse += "data: [DONE]\n\n";
        }

        const QByteArray response =
            "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: "
            + QByteArray::number(sse.size()) + "\r\nConnection: close\r\n\r\n" + sse;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

private:
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

} // namespace

class TestAgentSession : public QObject
{
    Q_OBJECT

private slots:
    void fullToolCallLoop();
    void serverSideSearchFiltersLocalSearchTool();
    void toolImagesReachModelAndSignal();

private:
    static QString collectText(const std::vector<Message> &history)
    {
        QString text;
        for (const Message &message : history)
            if (message.role == Role::Assistant)
                text += message.content;
        return text;
    }
};

void TestAgentSession::fullToolCallLoop()
{
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());

    MockChatServer server;
    QVERIFY(server.start());

    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());

    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    session.setRequestConfig(server.url().toString(), QStringLiteral("test-key"),
                             QStringLiteral("mock-model"));
    session.setSystemPrompt(QStringLiteral("test prompt"));
    session.setWorkdir(workdir.path());

    QStringList deltas;
    QList<Message> assistantMessages;
    QStringList toolCallIds;
    QStringList toolOutputs;
    QString failure;
    int idleCount = 0;

    connect(&session, &AgentSession::assistantDelta,
            [&](const QString &delta) { deltas.append(delta); });
    connect(&session, &AgentSession::assistantCompleted,
            [&](const Message &message) { assistantMessages.append(message); });
    connect(&session, &AgentSession::toolCallStarted,
            [&](const QString &id, const QString &name, const QString &args) {
                QVERIFY(!id.isEmpty());
                QCOMPARE(name, QStringLiteral("write"));
                QVERIFY(!args.isEmpty());
            });
    connect(&session, &AgentSession::toolCallFinished,
            [&](const QString &id, const QString &output) {
                toolCallIds.append(id);
                toolOutputs.append(output);
            });
    connect(&session, &AgentSession::failed,
            [&](const QString &message) { failure = message; });
    connect(&session, &AgentSession::idle, [&] { ++idleCount; });

    QEventLoop loop;
    connect(&session, &AgentSession::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit); // 防挂死
    session.sendUserMessage(QStringLiteral("把 note 写入工作文件夹"));
    loop.exec();

    // 传输与流式解析
    QCOMPARE(failure, QString());
    QCOMPARE(server.requestCount, 2);
    QVERIFY(deltas.contains(QStringLiteral("文件已写入")));
    QVERIFY(deltas.contains(QStringLiteral("，任务完成")));
    QVERIFY(session.busy() == false);

    // 第一个请求带 system 段与 tools，第二个请求带工具结果
    QVERIFY(server.bodies[0].contains("tools"));
    QVERIFY(server.bodies[0].contains("test prompt"));
    QVERIFY(server.bodies[1].contains("\"tool_call_id\""));
    QVERIFY(server.bodies[1].contains("call_abc"));

    // 工具真实执行了
    QCOMPARE(toolCallIds, QStringList{QStringLiteral("call_abc")});
    QVERIFY(toolOutputs.first().contains(QStringLiteral("已写入")));
    QFile note(workdir.filePath(QStringLiteral("note.txt")));
    QVERIFY(note.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(note.readAll()), QStringLiteral("written by mock"));

    // 历史与最终回复
    const QString text = collectText(session.history());
    QCOMPARE(text, QStringLiteral("文件已写入，任务完成"));
    bool hasToolMessage = false;
    for (const Message &message : session.history()) {
        if (message.role == Role::Tool && message.toolCallId == QStringLiteral("call_abc"))
            hasToolMessage = true;
    }
    QVERIFY(hasToolMessage);
    QCOMPARE(assistantMessages.size(), 2); // 工具调用消息 + 最终回复
    QCOMPARE(idleCount, 1);
    QCOMPARE(assistantMessages.last().toolCalls.size(), 0);

    // usage 随 Message 端到端透传：工具回合 + 最终回复各带一次
    QVERIFY(assistantMessages[0].usage.valid);
    QCOMPARE(assistantMessages[0].usage.promptTokens, 110);
    QCOMPARE(assistantMessages[0].usage.completionTokens, 25);
    QCOMPARE(assistantMessages[0].usage.cachedTokens, 60);
    QVERIFY(assistantMessages[1].usage.valid);
    QCOMPARE(assistantMessages[1].usage.promptTokens, 150);
    QCOMPARE(assistantMessages[1].usage.completionTokens, 12);

    // 会话历史可整体持久化回读
    QTemporaryDir dbDir;
    SessionStore store(dbDir.filePath(QStringLiteral("s.db")));
    QVERIFY(store.open());
    const qint64 id = store.createConversation(QStringLiteral("e2e"), workdir.path());
    for (const Message &message : session.history())
        QVERIFY(store.appendMessage(id, message));
    const auto persisted = store.messages(id);
    QCOMPARE(persisted.size(), session.history().size());
    QCOMPARE(persisted[1].toolCalls.first().name, QStringLiteral("write"));
    QVERIFY(persisted[1].usage.valid); // usage 列随消息一起持久化
    QCOMPARE(persisted[1].usage.promptTokens, 110);
    QCOMPARE(persisted[1].usage.completionTokens, 25);
}

// 服务端搜索开启：请求体带 web_search_options，本地 web_search 工具被过滤，write 保留
void TestAgentSession::serverSideSearchFiltersLocalSearchTool()
{
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());

    MockChatServer server;
    QVERIFY(server.start());

    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<WebSearchTool>()); // 未配置也不会被执行，仅进 spec

    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    session.setRequestConfig(server.url().toString(), QStringLiteral("k"),
                             QStringLiteral("mock-model"));
    session.setServerSideSearch(true);
    session.setWorkdir(workdir.path());

    QString failure;
    connect(&session, &AgentSession::failed,
            [&](const QString &message) { failure = message; });

    QEventLoop loop;
    connect(&session, &AgentSession::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    session.sendUserMessage(QStringLiteral("把 note 写入工作文件夹"));
    loop.exec();

    QCOMPARE(failure, QString());
    QCOMPARE(server.requestCount, 2);
    QVERIFY(server.bodies[0].contains("web_search_options")); // 服务端搜索参数已带上
    QVERIFY(server.bodies[0].contains("\"write\""));          // 其余工具保留
    QVERIFY(!server.bodies[0].contains("\"web_search\""));    // 本地搜索工具被过滤
    QFile note(workdir.filePath(QStringLiteral("note.txt")));
    QVERIFY(note.open(QIODevice::ReadOnly)); // 工具循环不受影响
    QCOMPARE(QString::fromUtf8(note.readAll()), QStringLiteral("written by mock"));
}

// read 工具读图片：ToolResult.images 经 toolCallFinished 发出并进入下一轮请求体
void TestAgentSession::toolImagesReachModelAndSignal()
{
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());
    const QByteArray png = QByteArray("\x89PNG\r\n\x1A\n", 8) + QByteArray("pixels");
    QFile logo(workdir.filePath(QStringLiteral("logo.png")));
    QVERIFY(logo.open(QIODevice::WriteOnly));
    QCOMPARE(logo.write(png), qint64(png.size()));
    logo.close();

    MockChatServer server;
    QVERIFY(server.start());
    server.toolName = "read";
    // JSON 字符串里的引号需要转义为 \\"，进入 SSE 后成为模型给出的参数文本
    server.toolArgs = "{\\\"path\\\":\\\"logo.png\\\"}";

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());

    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    session.setRequestConfig(server.url().toString(), QStringLiteral("k"),
                             QStringLiteral("mock-model"));
    session.setWorkdir(workdir.path());

    QString failure;
    QList<ImageAttachment> signalImages;
    connect(&session, &AgentSession::failed,
            [&](const QString &message) { failure = message; });
    connect(&session, &AgentSession::toolCallFinished,
            [&](const QString &, const QString &, const QList<ImageAttachment> &images) {
                signalImages = images;
            });

    QEventLoop loop;
    connect(&session, &AgentSession::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    session.sendUserMessage(QStringLiteral("看看 logo.png"));
    loop.exec();

    QCOMPARE(failure, QString());
    QCOMPARE(server.requestCount, 2);
    // 工具结果图片出现在下一轮请求体里（合成 user 消息的 image_url parts）
    QVERIFY(server.bodies[1].contains("image_url"));
    QVERIFY(server.bodies[1].contains("data:image/png;base64,"));
    QVERIFY(server.bodies[1].contains(png.toBase64()));
    // toolCallFinished 信号同样携带图片
    QCOMPARE(signalImages.size(), 1);
    QCOMPARE(signalImages.first().mimeType, QStringLiteral("image/png"));
    QCOMPARE(signalImages.first().data, png);
    // 历史中的 Tool 消息带图片，回灌请求经 messageToJson 序列化
    bool toolMessageHasImage = false;
    for (const Message &message : session.history()) {
        if (message.role == Role::Tool && !message.images.isEmpty())
            toolMessageHasImage = true;
    }
    QVERIFY(toolMessageHasImage);
}

QTEST_GUILESS_MAIN(TestAgentSession)
#include "test_agent_session.moc"
