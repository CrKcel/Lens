#include <QtTest/QtTest>

#include <lens/core/providers/ModelListClient.hpp>

#include "TestServerEnv.hpp"

using namespace lens;

// 模型清单拉取

class TestModelList : public QObject
{
    Q_OBJECT

private slots:
    void modelsEndpointResolution();
    void fetchFromRealServer();
    void fetchAnthropicFromRealServer();
    void connectionRefusedReported();

private:
    struct FetchResult
    {
        QStringList models;
        QString error;
        bool done = false;
    };

    void runFetch(ModelListClient &client, Protocol protocol, const QString &baseUrl,
                  const QString &apiKey, FetchResult &result)
    {
        client.fetch(protocol, baseUrl, apiKey,
                     [&result](QStringList models, QString error) {
                         result.models = std::move(models);
                         result.error = std::move(error);
                         result.done = true;
                     });
        QTRY_VERIFY(result.done);
    }
};

// 各协议模型清单端点：完整路径替换、仅主机/根路径补全
void TestModelList::modelsEndpointResolution()
{
    const auto chat = makeProtocolAdapter(Protocol::ChatCompletions);
    QCOMPARE(chat->resolveModelsEndpoint(QStringLiteral("https://api.openai.com/v1")).toString(),
             QStringLiteral("https://api.openai.com/v1/models"));
    QCOMPARE(chat->resolveModelsEndpoint(QStringLiteral("https://h/v1/chat/completions")).toString(),
             QStringLiteral("https://h/v1/models"));
    QCOMPARE(chat->resolveModelsEndpoint(QStringLiteral("https://h")).toString(),
             QStringLiteral("https://h/models"));

    const auto responses = makeProtocolAdapter(Protocol::Responses);
    QCOMPARE(responses->resolveModelsEndpoint(QStringLiteral("https://h/v1/responses")).toString(),
             QStringLiteral("https://h/v1/models"));
    QCOMPARE(responses->resolveModelsEndpoint(QStringLiteral("https://h/v1")).toString(),
             QStringLiteral("https://h/v1/models"));

    const auto anthropic = makeProtocolAdapter(Protocol::Anthropic);
    QCOMPARE(anthropic->resolveModelsEndpoint(QStringLiteral("https://h")).toString(),
             QStringLiteral("https://h/v1/models"));
    QCOMPARE(anthropic->resolveModelsEndpoint(QStringLiteral("https://h/v1")).toString(),
             QStringLiteral("https://h/v1/models"));
    QCOMPARE(anthropic->resolveModelsEndpoint(QStringLiteral("https://h/v1/messages")).toString(),
             QStringLiteral("https://h/v1/models"));
    QCOMPARE(
        anthropic->resolveModelsEndpoint(QStringLiteral("https://h/anthropic")).toString(),
        QStringLiteral("https://h/anthropic/v1/models"));
}

void TestModelList::fetchFromRealServer()
{
    test::TestServer server;
    LENS_REQUIRE_SERVER(server)

    ModelListClient client;
    FetchResult result;
    runFetch(client, Protocol::ChatCompletions, server.endpoint, server.apiKey, result);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    qInfo() << "models:" << result.models.join(QStringLiteral(", "));
    if (result.models.isEmpty())
        qWarning("服务器返回空模型清单");
}

void TestModelList::fetchAnthropicFromRealServer()
{
    test::TestServer server;
    LENS_REQUIRE_SERVER(server)

    ModelListClient client;
    FetchResult result;
    runFetch(client, Protocol::Anthropic, server.anthropicEndpoint, server.apiKey, result);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
}

// 连接被拒（无服务监听的端口）必须以错误收尾，而不是挂死或空成功
void TestModelList::connectionRefusedReported()
{
    ModelListClient client;
    FetchResult result;
    runFetch(client, Protocol::ChatCompletions, QStringLiteral("http://127.0.0.1:1"),
             {}, result);
    QVERIFY(result.models.isEmpty());
    QVERIFY(!result.error.isEmpty());
}

QTEST_GUILESS_MAIN(TestModelList)
#include "test_model_list.moc"
