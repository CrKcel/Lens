#include <QtTest/QtTest>

#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QTimer>

#include <lens/core/agent/AgentSession.hpp>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/tools/ToolRegistry.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

#include "TestServerEnv.hpp"

using namespace lens;

// 真实服务器端到端：三协议工具调用链路与多模态视觉。

class TestRealServer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void chatCompletionsToolLoop();
    void responsesToolLoop();
    void anthropicToolLoop();
    void multimodalVision();
    void multimodalVisionResponses();
    void multimodalVisionAnthropic();

private:
    void runToolCallLoop(Protocol protocol, const QString &fileName);
    void runVisionTurn(Protocol protocol, const QByteArray &imageData);

    test::TestServer m_server;
    QString m_workdir;
};

void TestRealServer::initTestCase()
{
    LENS_REQUIRE_SERVER(m_server)
    m_workdir = test::testWorkdir();
    qInfo() << "base endpoint:" << m_server.endpoint << "anthropic endpoint:"
            << m_server.anthropicEndpoint << "model:" << m_server.model
            << "workdir:" << m_workdir;
}

// 三协议共用：让模型调用 write 写入目标文件并复述，校验工具执行与最终回复
void TestRealServer::runToolCallLoop(Protocol protocol, const QString &fileName)
{
    LENS_REQUIRE_PROTOCOL(m_server, protocol, protocolToString(protocol).toUtf8().constData())

    QFile stale(QDir(m_workdir).filePath(fileName));
    if (stale.exists())
        stale.remove();

    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<ReadTool>());

    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    // Anthropic 兼容端点与 OpenAI 系同源但前缀不同时，按协议分别传端点
    const QString endpoint = protocol == Protocol::Anthropic ? m_server.anthropicEndpoint
                                                             : m_server.endpoint;
    session.setRequestConfig(endpoint, m_server.apiKey, m_server.model);
    session.setProtocol(protocol);
    session.setSystemPrompt(QStringLiteral(
        "你是 Lens 编程 Agent。需要读写文件时必须调用提供的工具。"));
    session.setWorkdir(m_workdir);

    QString reasoning;
    QString content;
    int toolCalls = 0;
    QString failure;
    int turns = 0;

    connect(&session, &AgentSession::assistantDelta,
            [&](const QString &delta) { content += delta; });
    connect(&session, &AgentSession::reasoningDelta,
            [&](const QString &delta) { reasoning += delta; });
    connect(&session, &AgentSession::toolCallStarted,
            [&](const QString &, const QString &, const QString &) { ++toolCalls; });
    connect(&session, &AgentSession::failed,
            [&](const QString &message) { failure = message; });
    connect(&session, &AgentSession::assistantCompleted, [&](const Message &) { ++turns; });

    QEventLoop loop;
    connect(&session, &AgentSession::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(180000, &loop, &QEventLoop::quit); // 4B 模型 + 思考，放宽时限
    session.sendUserMessage(QStringLiteral(
        "请调用 write 工具，把文本 hello-from-real-llama 写入 %1，然后告诉我结果。").arg(fileName));
    loop.exec();

    const QString protocolName = protocolToString(protocol);
    QVERIFY2(failure.isEmpty(),
             qPrintable(QStringLiteral("[%1] 会话失败: %2").arg(protocolName, failure)));
    QVERIFY(session.busy() == false);
    QVERIFY(turns >= 1);
    qInfo() << "[" << protocolName << "] turns:" << turns << "toolCalls:" << toolCalls
            << "reasoning chars:" << reasoning.size() << "content:" << content;

    bool finalAssistant = false;
    for (const Message &message : session.history()) {
        if (message.role == Role::Assistant && !message.content.isEmpty())
            finalAssistant = true;
    }
    QVERIFY2(finalAssistant, qPrintable(QStringLiteral("[%1] 缺少最终正文回复").arg(protocolName)));

    if (reasoning.isEmpty())
        qWarning("[%s] 未捕获到推理增量（服务端可能关闭了思考输出）", qPrintable(protocolName));

    if (toolCalls == 0)
        QFAIL(qPrintable(QStringLiteral(
            "[%1] 模型未发起任何工具调用——检查服务端是否启用 --jinja 与模型模板支持")
            .arg(protocolName)));
    QFile note(QDir(m_workdir).filePath(fileName));
    QVERIFY2(note.exists(), qPrintable(QStringLiteral("[%1] %2 未被创建").arg(protocolName, fileName)));
    QVERIFY(note.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(note.readAll()).trimmed(),
             QStringLiteral("hello-from-real-llama"));
}

void TestRealServer::chatCompletionsToolLoop()
{
    runToolCallLoop(Protocol::ChatCompletions, QStringLiteral("note.txt"));
}

void TestRealServer::responsesToolLoop()
{
    runToolCallLoop(Protocol::Responses, QStringLiteral("note-responses.txt"));
}

void TestRealServer::anthropicToolLoop()
{
    runToolCallLoop(Protocol::Anthropic, QStringLiteral("note-anthropic.txt"));
}

// 三协议共用：把图片发给多模态模型并要求描述，校验有正文回复且描述合理。
// 模型描述对不对是主观判断，逐字断言会脆：硬断言只保证「图片被接受且模型作答」，
// 关键词命中与否打日志供人工判断。
void TestRealServer::runVisionTurn(Protocol protocol, const QByteArray &imageData)
{
    LENS_REQUIRE_PROTOCOL(m_server, protocol, protocolToString(protocol).toUtf8().constData())
    QVERIFY2(!imageData.isEmpty(),
             "找不到测试图片（LENS_TEST_IMAGE 或 packaging/icons/lens-256.png）");

    ToolRegistry registry; // 不带工具：纯视觉问答
    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    const QString endpoint = protocol == Protocol::Anthropic ? m_server.anthropicEndpoint
                                                             : m_server.endpoint;
    session.setRequestConfig(endpoint, m_server.apiKey, m_server.model);
    session.setProtocol(protocol);
    session.setSystemPrompt(QStringLiteral("回答使用中文，简明扼要。"));

    QString content;
    QString failure;
    connect(&session, &AgentSession::assistantDelta,
            [&](const QString &delta) { content += delta; });
    connect(&session, &AgentSession::failed,
            [&](const QString &message) { failure = message; });

    QEventLoop loop;
    connect(&session, &AgentSession::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(180000, &loop, &QEventLoop::quit);
    session.sendUserMessage(QStringLiteral("请描述这张图片里是什么。"),
                            {{QStringLiteral("image/png"), imageData}});
    loop.exec();

    const QString protocolName = protocolToString(protocol);
    QVERIFY2(failure.isEmpty(),
             qPrintable(QStringLiteral("[%1] 会话失败: %2").arg(protocolName, failure)));
    QVERIFY2(!content.trimmed().isEmpty(),
             qPrintable(QStringLiteral("[%1] 模型未给出描述").arg(protocolName)));
    qInfo() << "[" << protocolName << "] 模型描述:" << content;

    const QStringList keywords = {QStringLiteral("蓝"), QStringLiteral("圆"),
                                  QStringLiteral("球"), QStringLiteral("镜"),
                                  QStringLiteral("blue"), QStringLiteral("circle"),
                                  QStringLiteral("sphere"), QStringLiteral("lens")};
    bool matched = false;
    for (const QString &keyword : keywords) {
        if (content.contains(keyword, Qt::CaseInsensitive)) {
            matched = true;
            break;
        }
    }
    if (!matched)
        qWarning("[%s] 描述未命中蓝/圆/球/镜等关键词，请人工核对描述内容",
                 qPrintable(protocolName));
}

void TestRealServer::multimodalVision()
{
    QFile icon(test::findTestImage());
    QVERIFY2(icon.open(QIODevice::ReadOnly), "打不开测试图片");
    runVisionTurn(Protocol::ChatCompletions, icon.readAll());
}

void TestRealServer::multimodalVisionResponses()
{
    QFile icon(test::findTestImage());
    QVERIFY2(icon.open(QIODevice::ReadOnly), "打不开测试图片");
    runVisionTurn(Protocol::Responses, icon.readAll());
}

void TestRealServer::multimodalVisionAnthropic()
{
    QFile icon(test::findTestImage());
    QVERIFY2(icon.open(QIODevice::ReadOnly), "打不开测试图片");
    runVisionTurn(Protocol::Anthropic, icon.readAll());
}

QTEST_GUILESS_MAIN(TestRealServer)
#include "test_real_server.moc"
