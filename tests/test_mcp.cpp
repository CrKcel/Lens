#include <QtTest/QtTest>

#include <lens/core/mcp/McpClient.hpp>
#include <lens/core/mcp/McpTool.hpp>
#include <lens/core/tools/ToolRegistry.hpp>

#include <QStringList>
#include <optional>

#include "TestServerEnv.hpp"

using namespace lens;

// MCP 客户端测试：对真实 stdio MCP 服务器实测。LENS_MCP_COMMAND 指定启动
// 命令（如 "npx -y @modelcontextprotocol/server-everything"），未设置时跳过
// 并警告；LENS_MCP_TOOL / LENS_MCP_TOOL_ARGS（JSON）指定实测的工具调用，
// 未设置时 tools/call 相关用例跳过并警告。

namespace {

std::optional<mcp::ServerConfig> realServerConfig()
{
    const QString command = qEnvironmentVariable("LENS_MCP_COMMAND").trimmed();
    if (command.isEmpty())
        return std::nullopt;
    const QStringList parts = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return mcp::ServerConfig{QStringLiteral("test"), parts.first(), parts.mid(1)};
}

nlohmann::json configuredToolArgs()
{
    const QString raw = qEnvironmentVariable("LENS_MCP_TOOL_ARGS").trimmed();
    nlohmann::json args = nlohmann::json::parse(raw.toStdString(), nullptr, false);
    if (args.is_discarded() || !args.is_object())
        args = nlohmann::json::object();
    return args;
}

// 跳过辅助：未配置真实 MCP 服务器时警告并跳过（在测试槽内使用）
#define LENS_REQUIRE_MCP_SERVER(var)                                                       \
    const auto var = realServerConfig();                                                   \
    if (!(var)) {                                                                          \
        qWarning("未设置 LENS_MCP_COMMAND（真实 stdio MCP 服务器），跳过");                  \
        QSKIP("未配置真实 MCP 服务器");                                                    \
    }

} // namespace

class TestMcp : public QObject
{
    Q_OBJECT

private slots:
    void startFailsWithBadCommand();
    void handshakeAndListTools();
    void unknownToolIsError();
    void callConfiguredTool();
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
    LENS_REQUIRE_MCP_SERVER(config)
    mcp::McpClient client(*config);
    QString error;
    QVERIFY(client.start(&error));
    QVERIFY(error.isEmpty());

    const auto tools = client.listTools(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    qInfo() << "MCP 工具数:" << tools.size();
    for (const auto &tool : tools)
        qInfo() << " -" << tool.name << tool.description;
    if (tools.empty())
        qWarning("服务器未暴露任何工具");
}

void TestMcp::unknownToolIsError()
{
    LENS_REQUIRE_MCP_SERVER(config)
    mcp::McpClient client(*config);
    QString error;
    QVERIFY(client.start(&error));

    const ToolResult result = client.callTool(QStringLiteral("lens-no-such-tool"),
                                              nlohmann::json::object());
    QVERIFY(!result.ok);
    QVERIFY(!result.output.isEmpty());
}

void TestMcp::callConfiguredTool()
{
    LENS_REQUIRE_MCP_SERVER(config)
    const QString toolName = qEnvironmentVariable("LENS_MCP_TOOL").trimmed();
    if (toolName.isEmpty()) {
        qWarning("未设置 LENS_MCP_TOOL（真实服务器上要实测调用的工具名），跳过");
        QSKIP("未配置实测工具");
    }

    mcp::McpClient client(*config);
    QString error;
    QVERIFY(client.start(&error));

    const ToolResult result = client.callTool(toolName, configuredToolArgs());
    QVERIFY2(result.ok, qPrintable(result.output));
}

void TestMcp::bridgedToolSatisfiesRegistry()
{
    LENS_REQUIRE_MCP_SERVER(config)
    auto client = std::make_shared<mcp::McpClient>(*config);
    QString error;
    QVERIFY(client->start(&error));
    const auto tools = client->listTools(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    if (tools.empty()) {
        qWarning("服务器未暴露任何工具，跳过桥接断言");
        QSKIP("服务器无工具");
    }

    ToolRegistry registry;
    for (const auto &info : tools)
        registry.registerTool(std::make_shared<mcp::McpTool>(config->name, info, client));

    // spec 进入注册表，数量与 listTools 一致
    const auto specs = registry.specs();
    QCOMPARE(specs.size(), tools.size());

    // 配置了实测工具时，execute 走 IBuiltinTool 通道
    const QString toolName = qEnvironmentVariable("LENS_MCP_TOOL").trimmed();
    if (toolName.isEmpty()) {
        qWarning("未设置 LENS_MCP_TOOL，跳过注册表 execute 断言");
        return;
    }
    const ToolResult result = registry.execute(toolName, configuredToolArgs(), QString());
    QVERIFY2(result.ok, qPrintable(result.output));
}

QTEST_GUILESS_MAIN(TestMcp)
#include "test_mcp.moc"
