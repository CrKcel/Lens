#include <QtTest/QtTest>

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QDir>

#include <lens/core/agent/AgentSession.hpp>
#include <lens/core/providers/QNetworkTransport.hpp>
#include <lens/core/tools/ToolRegistry.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>
#include <lens/core/tools/builtins/WriteTool.hpp>

using namespace lens;

namespace {
QString stripEndpointPath(const QString &endpoint)
{
    // LENS_REAL_SERVER 允许给完整 chat/completions 路径或 base：
    // 多协议测试统一退回 base，再由各协议适配器补全自家路径
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.endsWith(QLatin1String("/chat/completions")))
        base.chop(QStringView(u"/chat/completions").size());
    return base;
}

QByteArray readFileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
} // namespace

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
    QString pickModel();
    void runToolCallLoop(Protocol protocol, const QString &fileName);
    QString findIconImage() const;
    void runVisionTurn(Protocol protocol, const QByteArray &imageData);

    QString m_baseEndpoint;
    QString m_anthropicEndpoint; // Anthropic 兼容端点与 OpenAI 系不同前缀的供应商（如 DeepSeek）可覆盖
    QString m_apiKey;
    QString m_model;
    QString m_workdir;
};

void TestRealServer::initTestCase()
{
    const QString endpoint = qEnvironmentVariable("LENS_REAL_SERVER");
    if (endpoint.isEmpty())
        QSKIP("未设置 LENS_REAL_SERVER，跳过真实服务测试");
    m_baseEndpoint = stripEndpointPath(endpoint);
    m_workdir = qEnvironmentVariable("LENS_WORKDIR");
    if (m_workdir.isEmpty())
        m_workdir = QDir::temp().filePath(QStringLiteral("lens-real-test"));
    QDir().mkpath(m_workdir);
    m_anthropicEndpoint = [&] {
        const QString overrideEndpoint = qEnvironmentVariable("LENS_REAL_ANTHROPIC_ENDPOINT");
        return overrideEndpoint.isEmpty() ? m_baseEndpoint : overrideEndpoint;
    }();
    m_apiKey = qEnvironmentVariable("LENS_REAL_API_KEY");
    if (m_apiKey.isEmpty())
        m_apiKey = QStringLiteral("no-key-needed");
    m_model = pickModel();
    qInfo() << "base endpoint:" << m_baseEndpoint << "model:" << m_model << "workdir:" << m_workdir;
}

QString TestRealServer::pickModel()
{
    const QString fromEnv = qEnvironmentVariable("LENS_REAL_MODEL");
    if (!fromEnv.isEmpty())
        return fromEnv;

    QNetworkAccessManager nam;
    QUrl modelsUrl = QUrl(m_baseEndpoint);
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

// 三协议共用：让模型调用 write 写入目标文件并复述，校验工具执行与最终回复
void TestRealServer::runToolCallLoop(Protocol protocol, const QString &fileName)
{
    QFile stale(QDir(m_workdir).filePath(fileName));
    if (stale.exists())
        stale.remove();

    ToolRegistry registry;
    registry.registerTool(std::make_shared<WriteTool>());
    registry.registerTool(std::make_shared<ReadTool>());

    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    // Anthropic 兼容端点与 OpenAI 系同源但前缀不同时，此处按协议分别传端点
    const QString endpoint = protocol == Protocol::Anthropic ? m_anthropicEndpoint
                                                             : m_baseEndpoint;
    session.setRequestConfig(endpoint, m_apiKey, m_model);
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

// 项目图标：LENS_REAL_IMAGE 覆盖，否则从当前目录向上找 packaging/icons/lens-256.png
QString TestRealServer::findIconImage() const
{
    const QString fromEnv = qEnvironmentVariable("LENS_REAL_IMAGE");
    if (!fromEnv.isEmpty())
        return fromEnv;
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 4; ++i) {
        const QString candidate =
            dir.filePath(QStringLiteral("packaging/icons/lens-256.png"));
        if (QFileInfo::exists(candidate))
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return {};
}

// 三协议共用：把图片发给多模态模型并要求描述，校验有正文回复且描述合理。
// 模型描述对不对是主观判断，逐字断言会脆：硬断言只保证「图片被接受且模型作答」，
// 关键词命中与否打日志供人工判断。
void TestRealServer::runVisionTurn(Protocol protocol, const QByteArray &imageData)
{
    QVERIFY2(!imageData.isEmpty(),
             "找不到测试图片（LENS_REAL_IMAGE 或 packaging/icons/lens-256.png）");

    ToolRegistry registry; // 不带工具：纯视觉问答
    AgentSession session(std::make_unique<QNetworkTransport>(), &registry);
    const QString endpoint = protocol == Protocol::Anthropic ? m_anthropicEndpoint
                                                             : m_baseEndpoint;
    session.setRequestConfig(endpoint, m_apiKey, m_model);
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
    runVisionTurn(Protocol::ChatCompletions, readFileBytes(findIconImage()));
}

void TestRealServer::multimodalVisionResponses()
{
    runVisionTurn(Protocol::Responses, readFileBytes(findIconImage()));
}

void TestRealServer::multimodalVisionAnthropic()
{
    runVisionTurn(Protocol::Anthropic, readFileBytes(findIconImage()));
}

QTEST_GUILESS_MAIN(TestRealServer)
#include "test_real_server.moc"
