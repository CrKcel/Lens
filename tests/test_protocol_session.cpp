#include <QtTest/QtTest>

#include <QEventLoop>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <lens/core/agent/AgentSession.hpp>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/tools/ToolRegistry.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

using namespace lens;

namespace {

// 本地 mock 服务：第 1 个请求返回协议对应的工具调用流，第 2 个起返回文本流。
// anthropic 与 responses 两套 SSE 帧由 respond 内按需拼装。
class MockProtocolServer : public QObject
{
    Q_OBJECT

public:
    enum class Flavor { Anthropic, Responses };

    bool start()
    {
        connect(&m_server, &QTcpServer::newConnection, this,
                &MockProtocolServer::handleConnection);
        return m_server.listen(QHostAddress::LocalHost);
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
    }

    int requestCount = 0;
    QList<QByteArray> bodies;
    QList<QByteArray> authHeaders;
    Flavor flavor = Flavor::Anthropic;

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
        const int bodyLength = QByteArrayView(header).mid(lengthKey + 15).trimmed().toInt();
        const QByteArray body = raw.mid(headerEnd + 4);
        if (body.size() < bodyLength)
            return;

        const QByteArray fullHeader = raw.left(headerEnd);
        authHeaders.append(fullHeader);
        m_buffers.remove(socket);
        bodies.append(body);
        respond(socket, ++requestCount);
    }

    void respond(QTcpSocket *socket, int requestNumber)
    {
        QByteArray sse;
        if (requestNumber == 1) {
            if (flavor == Flavor::Anthropic) {
                // 文本增量 + 工具调用（参数分两个分片），无 [DONE] 哨兵
                sse += "event: message_start\ndata: {\"type\":\"message_start\"}\n\n";
                sse += "data: {\"type\":\"content_block_start\",\"index\":0,"
                       "\"content_block\":{\"type\":\"text\"}}\n\n";
                sse += "data: {\"type\":\"content_block_delta\",\"index\":0,"
                       "\"delta\":{\"type\":\"text_delta\",\"text\":\"我来写入\"}}\n\n";
                sse += "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n";
                sse += "data: {\"type\":\"content_block_start\",\"index\":1,"
                       "\"content_block\":{\"type\":\"tool_use\",\"id\":\"tu_abc\",\"name\":\"write\"}}\n\n";
                sse += "data: {\"type\":\"content_block_delta\",\"index\":1,"
                       "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"path\\\":\\\"note.txt\\\",\"}}\n\n";
                sse += "data: {\"type\":\"content_block_delta\",\"index\":1,"
                       "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"content\\\":\\\"written by mock\\\"}\"}}\n\n";
                sse += "data: {\"type\":\"content_block_stop\",\"index\":1}\n\n";
                sse += "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n\n";
                sse += "data: {\"type\":\"message_stop\"}\n\n";
            } else {
                sse += "data: {\"type\":\"response.output_text.delta\",\"delta\":\"我来写入\"}\n\n";
                sse += "data: {\"type\":\"response.output_item.added\",\"output_index\":1,"
                       "\"item\":{\"type\":\"function_call\",\"call_id\":\"fc_abc\",\"name\":\"write\"}}\n\n";
                sse += "data: {\"type\":\"response.function_call_arguments.delta\",\"output_index\":1,"
                       "\"delta\":\"{\\\"path\\\":\\\"note.txt\\\",\"}\n\n";
                sse += "data: {\"type\":\"response.function_call_arguments.delta\",\"output_index\":1,"
                       "\"delta\":\"\\\"content\\\":\\\"written by mock\\\"}\"}\n\n";
                sse += "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\"}}\n\n";
            }
        } else {
            if (flavor == Flavor::Anthropic) {
                sse += "data: {\"type\":\"content_block_start\",\"index\":0,"
                       "\"content_block\":{\"type\":\"text\"}}\n\n";
                sse += "data: {\"type\":\"content_block_delta\",\"index\":0,"
                       "\"delta\":{\"type\":\"text_delta\",\"text\":\"文件已写入，任务完成\"}}\n\n";
                sse += "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n\n";
                sse += "data: {\"type\":\"message_stop\"}\n\n";
            } else {
                sse += "data: {\"type\":\"response.output_text.delta\",\"delta\":\"文件已写入，任务完成\"}\n\n";
                sse += "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_2\"}}\n\n";
            }
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

class TestProtocolSession : public QObject
{
    Q_OBJECT

private slots:
    void anthropicToolCallLoop();
    void responsesToolCallLoop();

private:
    // 两个协议共用同一套驱动与断言：请求头/请求体/工具执行/历史
    void runToolCallLoop(MockProtocolServer::Flavor flavor);
};

void TestProtocolSession::runToolCallLoop(MockProtocolServer::Flavor flavor)
{
    QTemporaryDir workdir;
    QVERIFY(workdir.isValid());

    MockProtocolServer server;
    server.flavor = flavor;
    QVERIFY(server.start());

    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());

    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    session.setRequestConfig(server.url().toString(), QStringLiteral("secret-key"),
                             QStringLiteral("mock-model"));
    session.setProtocol(flavor == MockProtocolServer::Flavor::Anthropic ? Protocol::Anthropic
                                                                        : Protocol::Responses);
    session.setSystemPrompt(QStringLiteral("test prompt"));
    session.setWorkdir(workdir.path());

    QStringList deltas;
    QList<Message> assistantMessages;
    QStringList toolOutputs;
    QString failure;
    int idleCount = 0;
    connect(&session, &AgentSession::assistantDelta,
            [&](const QString &delta) { deltas.append(delta); });
    connect(&session, &AgentSession::assistantCompleted,
            [&](const Message &message) { assistantMessages.append(message); });
    connect(&session, &AgentSession::toolCallFinished,
            [&](const QString &id, const QString &output) {
                Q_UNUSED(id);
                toolOutputs.append(output);
            });
    connect(&session, &AgentSession::failed, [&](const QString &message) { failure = message; });
    connect(&session, &AgentSession::idle, [&] { ++idleCount; });

    QEventLoop loop;
    connect(&session, &AgentSession::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    session.sendUserMessage(QStringLiteral("把 note 写入工作文件夹"));
    loop.exec();

    QCOMPARE(failure, QString());
    QCOMPARE(server.requestCount, 2);
    QVERIFY(deltas.contains(QStringLiteral("我来写入")));
    QVERIFY(deltas.contains(QStringLiteral("文件已写入，任务完成")));
    QVERIFY(!session.busy());
    QCOMPARE(idleCount, 1);

    // 鉴权头按协议生效：anthropic 用 x-api-key，responses 用 Bearer
    // （Qt 规范化请求头名大小写，比较时忽略大小写）
    QVERIFY(!server.authHeaders.isEmpty());
    const QString requestHeader = QString::fromLatin1(server.authHeaders.first());
    if (flavor == MockProtocolServer::Flavor::Anthropic)
        QVERIFY(requestHeader.contains(QStringLiteral("x-api-key"), Qt::CaseInsensitive)
                && requestHeader.contains(QStringLiteral("secret-key")));
    else
        QVERIFY(requestHeader.contains(QStringLiteral("Authorization: Bearer secret-key"),
                                       Qt::CaseInsensitive));

    const auto parse = [](const QByteArray &body) {
        return nlohmann::json::parse(body.constData(), body.constData() + body.size(), nullptr,
                                     false);
    };
    const auto first = parse(server.bodies[0]);
    QVERIFY(!first.is_discarded());
    QVERIFY(first.contains("tools"));
    QVERIFY(first.contains("model"));

    // 第二个请求带回了工具结果（协议各自的回传形态）
    const auto second = parse(server.bodies[1]);
    QVERIFY(!second.is_discarded());
    if (flavor == MockProtocolServer::Flavor::Anthropic) {
        QVERIFY(server.bodies[0].contains("input_schema"));
        QVERIFY(server.bodies[0].contains("test prompt"));
        QVERIFY(server.bodies[1].contains("tool_result"));
        QVERIFY(server.bodies[1].contains("tu_abc"));
    } else {
        QVERIFY(server.bodies[0].contains("instructions"));
        QVERIFY(server.bodies[1].contains("function_call_output"));
        QVERIFY(server.bodies[1].contains("fc_abc"));
    }

    // 工具真实执行，最终回复完整
    QVERIFY(toolOutputs.first().contains(QStringLiteral("已写入")));
    QFile note(workdir.filePath(QStringLiteral("note.txt")));
    QVERIFY(note.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(note.readAll()), QStringLiteral("written by mock"));

    QString text;
    for (const Message &message : session.history())
        if (message.role == Role::Assistant)
            text += message.content;
    QCOMPARE(text, QStringLiteral("我来写入文件已写入，任务完成"));
    QCOMPARE(assistantMessages.size(), 2);
    QCOMPARE(assistantMessages.first().toolCalls.size(), 1);
    QCOMPARE(assistantMessages.last().toolCalls.size(), 0);
}

void TestProtocolSession::anthropicToolCallLoop()
{
    runToolCallLoop(MockProtocolServer::Flavor::Anthropic);
}

void TestProtocolSession::responsesToolCallLoop()
{
    runToolCallLoop(MockProtocolServer::Flavor::Responses);
}

QTEST_GUILESS_MAIN(TestProtocolSession)
#include "test_protocol_session.moc"
