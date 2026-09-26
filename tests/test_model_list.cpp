#include <QtTest/QtTest>

#include <QTcpServer>
#include <QTcpSocket>
#include <lens/core/providers/ModelListClient.hpp>

using namespace lens;

namespace {

// 本地 mock HTTP 服务：应答单次 GET，记录请求原文供断言
class MockHttpServer
{
public:
    MockHttpServer()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, [this] { serve(); });
        m_server.listen(QHostAddress::LocalHost);
    }

    QString base() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    int status = 200;
    QByteArray body;
    QByteArray request; // 最后一次请求原文

private:
    void serve()
    {
        QTcpSocket *socket = m_server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, [this, socket] {
            request += socket->readAll();
            if (!request.contains("\r\n\r\n") || m_responded)
                return;
            m_responded = true;
            const char *reason = status == 200 ? "OK" : "Error";
            const QByteArray response =
                "HTTP/1.1 " + QByteArray::number(status) + " " + reason
                + "\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            socket->write(response);
            socket->disconnectFromHost();
        });
    }

    QTcpServer m_server;
    bool m_responded = false;
};

} // namespace

class TestModelList : public QObject
{
    Q_OBJECT

private slots:
    void modelsEndpointResolution();
    void fetchOpenAiStyle();
    void fetchAnthropicStyle();
    void emptyDataArrayIsSuccess();
    void httpErrorReported();
    void malformedBodyReported();

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

    // Qt 发送请求头时会把名称规范化为首字母大写（X-Api-Key），按不区分大小写匹配
    bool requestHasHeader(const MockHttpServer &server, const QByteArray &name,
                          const QByteArray &value) const
    {
        return server.request.toLower().contains(name.toLower() + ": " + value.toLower());
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

void TestModelList::fetchOpenAiStyle()
{
    MockHttpServer server;
    server.body = QByteArrayLiteral("{\"data\":[{\"id\":\"m-b\"},{\"id\":\"m-a\"},{\"id\":\"m-b\"}]}");

    ModelListClient client;
    FetchResult result;
    runFetch(client, Protocol::ChatCompletions, server.base() + "/v1",
             QStringLiteral("sk-test"), result);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.models, (QStringList{QStringLiteral("m-a"), QStringLiteral("m-b")}));
    QVERIFY(server.request.startsWith("GET /v1/models "));
    QVERIFY(requestHasHeader(server, "Authorization", "Bearer sk-test"));
}

void TestModelList::fetchAnthropicStyle()
{
    MockHttpServer server;
    server.body = QByteArrayLiteral(
        "{\"data\":[{\"type\":\"model\",\"id\":\"claude-2\"},{\"id\":\"claude-1\"}]}");

    ModelListClient client;
    FetchResult result;
    runFetch(client, Protocol::Anthropic, server.base(), QStringLiteral("sk-an"),
             result);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.models, (QStringList{QStringLiteral("claude-1"), QStringLiteral("claude-2")}));
    QVERIFY(server.request.startsWith("GET /v1/models "));
    QVERIFY(requestHasHeader(server, "x-api-key", "sk-an"));
    QVERIFY(requestHasHeader(server, "anthropic-version", "2023-06-01"));
}

// data 为空数组是合法响应：成功返回空清单而非报错
void TestModelList::emptyDataArrayIsSuccess()
{
    MockHttpServer server;
    server.body = QByteArrayLiteral("{\"data\":[]}");

    ModelListClient client;
    FetchResult result;
    runFetch(client, Protocol::ChatCompletions, server.base() + "/v1", {}, result);
    QVERIFY(result.error.isEmpty());
    QVERIFY(result.models.isEmpty());
}

void TestModelList::httpErrorReported()
{
    MockHttpServer server;
    server.status = 404;
    server.body = QByteArrayLiteral("{\"error\":{\"message\":\"no such route\"}}");

    ModelListClient client;
    FetchResult result;
    runFetch(client, Protocol::ChatCompletions, server.base() + "/v1",
             QStringLiteral("sk-test"), result);
    QVERIFY(result.models.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("404")));
    QVERIFY(result.error.contains(QStringLiteral("no such route")));
}

void TestModelList::malformedBodyReported()
{
    MockHttpServer server;
    server.body = QByteArrayLiteral("<html>gateway error</html>");

    ModelListClient client;
    FetchResult result;
    runFetch(client, Protocol::Responses, server.base() + "/v1", {}, result);
    QVERIFY(result.models.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("data")));
}

QTEST_GUILESS_MAIN(TestModelList)
#include "test_model_list.moc"
