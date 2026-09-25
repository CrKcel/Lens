#include <QtTest/QtTest>

#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>

#include <lens/core/tools/builtins/WebSearchTool.hpp>

using namespace lens;

namespace {

// 本地 mock 搜索服务（Tavily /search 兼容）：校验 Authorization 头，返回固定结果
class MockSearchServer : public QObject
{
    Q_OBJECT

public:
    bool start()
    {
        connect(&m_server, &QTcpServer::newConnection, this, &MockSearchServer::handleConnection);
        return m_server.listen(QHostAddress::LocalHost);
    }

    QString endpoint() const
    {
        return QStringLiteral("http://127.0.0.1:%1/search").arg(m_server.serverPort());
    }

    QByteArray lastAuthHeader;
    QByteArray lastBody;

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
        const QByteArray header = raw.left(headerEnd);
        const int authKey = header.indexOf("Authorization:");
        lastAuthHeader =
            authKey >= 0
                ? header.mid(authKey, header.indexOf('\n', authKey) - authKey).trimmed()
                : QByteArray();
        const QList<QByteArray> headerLines = header.split('\n');
        int bodyLength = -1;
        for (const QByteArray &line : headerLines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith("Content-Length:")) {
                bodyLength = trimmed.mid(15).toInt();
                break;
            }
        }
        if (bodyLength < 0)
            return;
        if (raw.size() < headerEnd + 4 + bodyLength)
            return; // 请求体尚未到齐
        lastBody = raw.mid(headerEnd + 4, bodyLength);
        m_buffers.remove(socket);

        const QByteArray body =
            "{\"results\":[{\"title\":\"Lens 项目\",\"url\":\"https://example.com/lens\","
            "\"content\":\"一个轻量的 GUI AI Agent\"},{\"title\":\"第二条\","
            "\"url\":\"https://example.com/2\",\"content\":\"简介\"}]}";
        const QByteArray response =
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

private:
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

} // namespace

class TestWebSearch : public QObject
{
    Q_OBJECT

private slots:
    void unconfiguredReturnsError();
    void missingQueryReturnsError();
    void searchReturnsFormattedResults();
};

void TestWebSearch::unconfiguredReturnsError()
{
    WebSearchTool tool;
    const ToolResult result = tool.execute(nlohmann::json{{"query", "任何"}}, QString());
    QVERIFY(!result.ok);
    QVERIFY(result.output.contains(QStringLiteral("未配置")));
}

void TestWebSearch::missingQueryReturnsError()
{
    WebSearchTool tool;
    tool.setConfig(QStringLiteral("http://127.0.0.1:1/search"), QStringLiteral("k"));
    const ToolResult result = tool.execute(nlohmann::json::object(), QString());
    QVERIFY(!result.ok);
    QVERIFY(result.output.contains(QStringLiteral("query")));
}

void TestWebSearch::searchReturnsFormattedResults()
{
    MockSearchServer server;
    QVERIFY(server.start());

    WebSearchTool tool;
    tool.setConfig(server.endpoint(), QStringLiteral("test-key"));
    const ToolResult result = tool.execute(nlohmann::json{{"query", "Lens 项目"}}, QString());

    QVERIFY(result.ok);
    QVERIFY(result.output.contains(QStringLiteral("Lens 项目")));
    QVERIFY(result.output.contains(QStringLiteral("https://example.com/lens")));
    QVERIFY(result.output.contains(QStringLiteral("一个轻量的 GUI AI Agent")));
    QCOMPARE(server.lastAuthHeader, QByteArrayLiteral("Authorization: Bearer test-key"));
    // QJsonDocument 会把非 ASCII 转义，解析后比较字段
    const QJsonDocument sent = QJsonDocument::fromJson(server.lastBody);
    QVERIFY(sent.isObject());
    QCOMPARE(sent.object().value("query").toString(), QStringLiteral("Lens 项目"));
    QCOMPARE(sent.object().value("max_results").toInt(), 5);
}

QTEST_GUILESS_MAIN(TestWebSearch)
#include "test_web_search.moc"
