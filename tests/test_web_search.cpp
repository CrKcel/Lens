#include <QtTest/QtTest>

#include <lens/core/tools/builtins/WebSearchTool.hpp>

using namespace lens;

// web_search 工具：参数校验与未配置报错是纯单测；真实搜索用例需要
// Tavily 兼容端点（LENS_TEST_SEARCH_ENDPOINT / LENS_TEST_SEARCH_KEY），
// 未配置时跳过并警告。

class TestWebSearch : public QObject
{
    Q_OBJECT

private slots:
    void unconfiguredReturnsError();
    void missingQueryReturnsError();
    void realEndpointSearch();
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

void TestWebSearch::realEndpointSearch()
{
    const QString endpoint = qEnvironmentVariable("LENS_TEST_SEARCH_ENDPOINT").trimmed();
    if (endpoint.isEmpty()) {
        qWarning("未设置 LENS_TEST_SEARCH_ENDPOINT（Tavily 兼容搜索端点），跳过真实搜索用例");
        QSKIP("未配置真实搜索端点");
    }
    const QString key = qEnvironmentVariable("LENS_TEST_SEARCH_KEY");

    WebSearchTool tool;
    tool.setConfig(endpoint, key);
    const ToolResult result =
        tool.execute(nlohmann::json{{"query", "Lens GUI AI Agent"}}, QString());
    QVERIFY2(result.ok, qPrintable(result.output));
    QVERIFY(!result.output.trimmed().isEmpty());
}

QTEST_GUILESS_MAIN(TestWebSearch)
#include "test_web_search.moc"
