#include <QtTest/QtTest>

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <lens/core/agent/AgentSession.hpp>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/tools/ToolRegistry.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

using namespace lens;

class TestRealServer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void chatWithReasoningAndTools();

private:
    QString pickModel();
    QString m_endpoint;
    QString m_model;
    QString m_workdir;
};

void TestRealServer::initTestCase()
{
    m_endpoint = qEnvironmentVariable("LENS_REAL_SERVER");
    if (m_endpoint.isEmpty())
        QSKIP("未设置 LENS_REAL_SERVER，跳过真实服务测试");
    m_workdir = qEnvironmentVariable("LENS_WORKDIR");
    if (m_workdir.isEmpty())
        m_workdir = QDir::temp().filePath(QStringLiteral("lens-real-test"));
    QDir().mkpath(m_workdir);
    m_model = pickModel();
    qInfo() << "endpoint:" << m_endpoint << "model:" << m_model << "workdir:" << m_workdir;
}

QString TestRealServer::pickModel()
{
    const QString fromEnv = qEnvironmentVariable("LENS_REAL_MODEL");
    if (!fromEnv.isEmpty())
        return fromEnv;

    QNetworkAccessManager nam;
    QUrl modelsUrl = QUrl(m_endpoint);
    modelsUrl.setPath(QStringLiteral("/v1/models"));
    QNetworkReply *reply = nam.get(QNetworkRequest(modelsUrl));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    const QJsonArray data = doc.object().value("data").toArray();
    if (!data.isEmpty())
        return data.at(0).toObject().value("id").toString();
    return QStringLiteral("default");
}

void TestRealServer::chatWithReasoningAndTools()
{
    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<ReadTool>());

    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    session.setRequestConfig(m_endpoint, QStringLiteral("no-key-needed"), m_model);
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
        "请调用 write 工具，把文本 hello-from-real-llama 写入 note.txt，然后告诉我结果。"));
    loop.exec();

    QVERIFY2(failure.isEmpty(), qPrintable(QStringLiteral("会话失败: %1").arg(failure)));
    QVERIFY(session.busy() == false);
    QVERIFY(turns >= 1);
    qInfo() << "turns:" << turns << "toolCalls:" << toolCalls
            << "reasoning chars:" << reasoning.size() << "content:" << content;

    bool finalAssistant = false;
    for (const Message &message : session.history()) {
        if (message.role == Role::Assistant && !message.content.isEmpty())
            finalAssistant = true;
    }
    QVERIFY2(finalAssistant, "缺少最终正文回复");

    if (reasoning.isEmpty())
        qWarning("未捕获到 reasoning_content（服务端可能关闭了思考输出）");

    if (toolCalls == 0)
        QFAIL("模型未发起任何工具调用——检查 llama.cpp 是否启用 --jinja 与模型模板支持");
    QFile note(QDir(m_workdir).filePath(QStringLiteral("note.txt")));
    QVERIFY2(note.exists(), "note.txt 未被创建");
    QVERIFY(note.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(note.readAll()).trimmed(),
             QStringLiteral("hello-from-real-llama"));
}

QTEST_GUILESS_MAIN(TestRealServer)
#include "test_real_server.moc"
