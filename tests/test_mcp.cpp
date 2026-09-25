#include <QtTest/QtTest>

#include <lens/core/mcp/McpClient.hpp>
#include <lens/core/mcp/McpTool.hpp>
#include <lens/core/tools/ToolRegistry.hpp>

#include <QDir>
#include <QFileInfo>
#include <QProcess>

using namespace lens;

namespace {

// 定位随测试构建的 mock 服务器可执行文件
QString mockServerPath()
{
    const QString path = QStringLiteral(MOCK_SERVER_PATH);
    return QFileInfo(path).absoluteFilePath();
}

mcp::ServerConfig makeConfig()
{
    return {QStringLiteral("mock"), mockServerPath(), {}};
}

} // namespace

class TestMcp : public QObject
{
    Q_OBJECT

private slots:
    void startFailsWithBadCommand();
    void handshakeAndListTools();
    void callToolEcho();
    void callToolFailure();
    void unknownToolIsError();
    void bridgedToolSatisfiesRegistry();
};

void TestMcp::startFailsWithBadCommand()
{
    mcp::McpClient client({QStringLiteral("bad"), QStringLiteral("/nonexistent-cmd-xyz"), {}});
    QString error;
    QVERIFY(!client.start(&error));
    QVERIFY(!error.isEmpty());
}

void TestMcp::handshakeAndListTools()
{
    mcp::McpClient client(makeConfig());
    QString error;
    QVERIFY(client.start(&error));
    QVERIFY(error.isEmpty());

    const auto tools = client.listTools(&error);
    QVERIFY(error.isEmpty());
    QCOMPARE(tools.size(), 2);

    bool foundEcho = false;
    for (const auto &tool : tools) {
        if (tool.name == QLatin1String("echo")) {
            foundEcho = true;
            QCOMPARE(tool.description, QStringLiteral("回显输入文本"));
            QCOMPARE(tool.inputSchema.at("type").get<std::string>(), "object");
        }
    }
    QVERIFY(foundEcho);
}

void TestMcp::callToolEcho()
{
    mcp::McpClient client(makeConfig());
    QString error;
    QVERIFY(client.start(&error));

    const ToolResult result =
        client.callTool(QStringLiteral("echo"), nlohmann::json{{"text", "你好世界"}});
    QVERIFY(result.ok);
    QCOMPARE(result.output, QStringLiteral("echo: 你好世界"));
}

void TestMcp::callToolFailure()
{
    mcp::McpClient client(makeConfig());
    QString error;
    QVERIFY(client.start(&error));

    const ToolResult result = client.callTool(QStringLiteral("fail"), nlohmann::json::object());
    QVERIFY(!result.ok);
}

void TestMcp::unknownToolIsError()
{
    mcp::McpClient client(makeConfig());
    QString error;
    QVERIFY(client.start(&error));

    const ToolResult result =
        client.callTool(QStringLiteral("nope"), nlohmann::json::object());
    QVERIFY(!result.ok);
}

void TestMcp::bridgedToolSatisfiesRegistry()
{
    auto client = std::make_shared<mcp::McpClient>(makeConfig());
    QString error;
    QVERIFY(client->start(&error));
    const auto tools = client->listTools(&error);
    QVERIFY(error.isEmpty());

    ToolRegistry registry;
    for (const auto &info : tools)
        registry.registerTool(std::make_shared<mcp::McpTool>(QStringLiteral("mock"), info, client));

    // spec 进入注册表，execute 走 IBuiltinTool 通道
    const auto specs = registry.specs();
    QCOMPARE(specs.size(), 2);
    const ToolResult result = registry.execute(
        QStringLiteral("echo"), nlohmann::json{{"text", "via registry"}}, QString());
    QVERIFY(result.ok);
    QCOMPARE(result.output, QStringLiteral("echo: via registry"));
}

QTEST_GUILESS_MAIN(TestMcp)
#include "test_mcp.moc"
