#include <QtTest/QtTest>

#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <lens/core/agent/AgentSession.hpp>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/storage/SessionStore.hpp>
#include <lens/core/tools/ToolRegistry.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

#include "TestServerEnv.hpp"

using namespace lens;

// AgentSession 端到端（默认 http://127.0.0.1:8081）。
// 请求体构造与 SSE 解析的确定性回归由 test_protocols / test_chat_completions
// 在无服务器的协议层覆盖；本文件只验会话级真实链路。

class TestAgentSession : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void fullToolCallLoop();
    void toolImagesReachSignal();

private:
    test::TestServer m_server;
};

void TestAgentSession::initTestCase()
{
    LENS_REQUIRE_SERVER(m_server)
    qInfo() << "测试服务器:" << m_server.endpoint << "模型:" << m_server.model;
}

// 工具调用循环：让模型调 write 写入指定文本，校验工具真实执行、历史完整、
// usage 透传与会话历史整体持久化回读
void TestAgentSession::fullToolCallLoop()
{
    const QString workdir = test::testWorkdir();
    const QString fileName = QStringLiteral("note-agent-session.txt");
    QFile stale(QDir(workdir).filePath(fileName));
    if (stale.exists())
        stale.remove();

    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());

    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    session.setRequestConfig(m_server.endpoint, m_server.apiKey, m_server.model);
    session.setSystemPrompt(
        QStringLiteral("你是 Lens 编程 Agent。需要写文件时必须调用提供的工具。"));
    session.setWorkdir(workdir);

    QString content;
    int toolCalls = 0;
    QList<Message> assistantMessages;
    QString failure;
    int idleCount = 0;

    connect(&session, &AgentSession::assistantDelta,
            [&](const QString &delta) { content += delta; });
    connect(&session, &AgentSession::assistantCompleted,
            [&](const Message &message) { assistantMessages.append(message); });
    connect(&session, &AgentSession::toolCallStarted,
            [&](const QString &, const QString &, const QString &) { ++toolCalls; });
    connect(&session, &AgentSession::failed,
            [&](const QString &message) { failure = message; });
    connect(&session, &AgentSession::idle, [&] { ++idleCount; });

    QEventLoop loop;
    connect(&session, &AgentSession::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(180000, &loop, &QEventLoop::quit); // 4B 模型 + 思考，放宽时限
    session.sendUserMessage(QStringLiteral(
        "请调用 write 工具，把文本 hello-from-lens 写入 %1，然后告诉我结果。").arg(fileName));
    loop.exec();

    QVERIFY2(failure.isEmpty(), qPrintable(QStringLiteral("会话失败: %1").arg(failure)));
    QVERIFY(!session.busy());
    QCOMPARE(idleCount, 1);
    QVERIFY2(toolCalls >= 1,
             "模型未发起任何工具调用——检查服务端是否启用 --jinja 与模型模板支持");

    // 工具真实执行，内容与指令一致
    QFile note(QDir(workdir).filePath(fileName));
    QVERIFY2(note.exists(), "目标文件未被创建");
    QVERIFY(note.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(note.readAll()).trimmed(),
             QStringLiteral("hello-from-lens"));

    // 历史里有工具消息与最终正文回复，最后一轮 assistant 不再带工具调用
    bool hasToolMessage = false;
    QString finalContent;
    for (const Message &message : session.history()) {
        if (message.role == Role::Tool)
            hasToolMessage = true;
        if (message.role == Role::Assistant && message.toolCalls.empty() && !message.content.isEmpty())
            finalContent = message.content;
    }
    QVERIFY(hasToolMessage);
    QVERIFY2(!finalContent.trimmed().isEmpty(), "缺少最终正文回复");
    QVERIFY(!assistantMessages.empty());
    QVERIFY(assistantMessages.last().toolCalls.empty());

    // usage 随 Message 端到端透传（服务器未上报则警告，不判失败）
    const Message &finalMessage = assistantMessages.last();
    if (!finalMessage.usage.valid)
        qWarning("服务器未上报 usage");
    else {
        QVERIFY(finalMessage.usage.promptTokens > 0);
        QVERIFY(finalMessage.usage.completionTokens > 0);
    }

    // 会话历史可整体持久化回读
    QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    SessionStore store(dbDir.filePath(QStringLiteral("s.db")));
    QVERIFY(store.open());
    const qint64 id = store.createConversation(QStringLiteral("e2e"), workdir);
    for (const Message &message : session.history())
        QVERIFY(store.appendMessage(id, message));
    const auto persisted = store.messages(id);
    QCOMPARE(persisted.size(), session.history().size());
    bool persistedToolCall = false;
    for (const Message &message : persisted) {
        if (message.role == Role::Assistant && !message.toolCalls.empty())
            persistedToolCall = true;
    }
    QVERIFY(persistedToolCall);
}

// read 工具读图片：ToolResult.images 经 toolCallFinished 发出（图片进入下一轮
// 请求体的序列化形态由 test_protocols 覆盖）。服务器需支持视觉输入
void TestAgentSession::toolImagesReachSignal()
{
    const QString imagePath = test::findTestImage();
    QVERIFY2(!imagePath.isEmpty(),
             "找不到测试图片（LENS_TEST_IMAGE 或 packaging/icons/lens-256.png）");
    QFile imageFile(imagePath);
    QVERIFY(imageFile.open(QIODevice::ReadOnly));
    const QByteArray imageData = imageFile.readAll();

    const QString workdir = test::testWorkdir();
    const QString target = QDir(workdir).filePath(QStringLiteral("logo.png"));
    QFile::remove(target);
    QVERIFY2(QFile::copy(imagePath, target), "复制测试图片到工作目录失败");

    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());

    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    session.setRequestConfig(m_server.endpoint, m_server.apiKey, m_server.model);
    session.setWorkdir(workdir);

    QList<ImageAttachment> signalImages;
    int toolCalls = 0;
    QString failure;
    connect(&session, &AgentSession::failed,
            [&](const QString &message) { failure = message; });
    connect(&session, &AgentSession::toolCallStarted,
            [&](const QString &, const QString &, const QString &) { ++toolCalls; });
    connect(&session, &AgentSession::toolCallFinished,
            [&](const QString &, const QString &, const QList<ImageAttachment> &images) {
                if (signalImages.isEmpty() && !images.isEmpty())
                    signalImages = images;
            });

    QEventLoop loop;
    connect(&session, &AgentSession::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(180000, &loop, &QEventLoop::quit);
    session.sendUserMessage(QStringLiteral(
        "请调用 read 工具读取工作目录里的 logo.png，然后用一句话描述它。"));
    loop.exec();

    // 图片请求可能被不支持视觉的服务端拒绝：跳过并警告，不算测试失败
    if (!failure.isEmpty()) {
        qWarning("会话失败（服务端可能不支持视觉输入），跳过: %s", qPrintable(failure));
        QSKIP("服务端不接受图片回合");
    }
    QVERIFY2(toolCalls >= 1, "模型未调用 read 工具");

    QVERIFY2(!signalImages.isEmpty(), "toolCallFinished 未携带图片");
    QCOMPARE(signalImages.first().mimeType, QStringLiteral("image/png"));
    QCOMPARE(signalImages.first().data, imageData);

    // 历史中的 Tool 消息带图片
    bool toolMessageHasImage = false;
    for (const Message &message : session.history()) {
        if (message.role == Role::Tool && !message.images.isEmpty())
            toolMessageHasImage = true;
    }
    QVERIFY(toolMessageHasImage);
}

QTEST_GUILESS_MAIN(TestAgentSession)
#include "test_agent_session.moc"
