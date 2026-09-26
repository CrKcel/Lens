#include <QtTest/QtTest>

#include <lens/core/context/PromptAssembler.hpp>
#include <lens/core/providers/ChatCompletionsClient.hpp>
#include <lens/core/providers/SseParser.hpp>
#include <lens/core/tools/ToolRegistry.hpp>
#include <lens/core/tools/builtins/BashTool.hpp>
#include <lens/core/tools/builtins/ReadTool.hpp>

using namespace lens;

class TestChatCompletions : public QObject
{
    Q_OBJECT

private slots:
    void buildsRequestWithRolesAndStream();
    void serializesToolCallsInHistory();
    void ignoresSystemMessagesInHistory();
    void carriesToolsInRequest();
    void extractsDeltaText();
    void toleratesMalformedPayload();
    void streamAccumulatesContentAndToolCalls();
    void llamaCppStreamWithReasoning();
    void streamParsesUsageFromFinalChunk();
    void sseParserHandlesSplitChunks();
    void promptAssemblerOverridesSections();
    void toolRegistryEmitsFunctionFormat();
};

void TestChatCompletions::buildsRequestWithRolesAndStream()
{
    const std::vector<Message> history{
        {Role::User, QStringLiteral("hi"), {}, {}, {}, {}},
        {Role::Assistant, QStringLiteral("hello"), {}, {}, {}, {}},
    };
    const auto body = chatcompletions::buildRequestBody(
        history, QStringLiteral("test-model"), QStringLiteral("you are lens"), true);

    QVERIFY(body["model"].get<std::string>() == "test-model");
    QVERIFY(body["stream"].get<bool>());
    QCOMPARE(body["messages"].size(), 3);
    QVERIFY(body["messages"][0]["role"].get<std::string>() == "system");
    QVERIFY(body["messages"][0]["content"].get<std::string>() == "you are lens");
    QVERIFY(body["messages"][1]["role"].get<std::string>() == "user");
    QVERIFY(body["messages"][1]["content"].get<std::string>() == "hi");
    QVERIFY(body["messages"][2]["role"].get<std::string>() == "assistant");
}

void TestChatCompletions::serializesToolCallsInHistory()
{
    Message assistant;
    assistant.role = Role::Assistant;
    assistant.toolCalls.append({QStringLiteral("call_1"), QStringLiteral("read"),
                                QStringLiteral("{\"path\":\"a\"}")});
    Message tool;
    tool.role = Role::Tool;
    tool.content = QStringLiteral("contents");
    tool.toolCallId = QStringLiteral("call_1");

    const std::vector<Message> history{assistant, tool};
    const auto body = chatcompletions::buildRequestBody(history, QStringLiteral("m"), {}, true);

    QCOMPARE(body["messages"].size(), 2);
    const auto &serializedCall = body["messages"][0]["tool_calls"][0];
    QVERIFY(serializedCall["id"].get<std::string>() == "call_1");
    QVERIFY(serializedCall["type"].get<std::string>() == "function");
    QVERIFY(serializedCall["function"]["name"].get<std::string>() == "read");
    QVERIFY(serializedCall["function"]["arguments"].get<std::string>() == "{\"path\":\"a\"}");
    QVERIFY(body["messages"][1]["role"].get<std::string>() == "tool");
    QVERIFY(body["messages"][1]["tool_call_id"].get<std::string>() == "call_1");
}

void TestChatCompletions::ignoresSystemMessagesInHistory()
{
    const std::vector<Message> history{
        {Role::System, QStringLiteral("stray system"), {}, {}, {}, {}},
        {Role::User, QStringLiteral("question"), {}, {}, {}, {}},
    };
    // systemPrompt 为空时不插入 system 段，历史中的 System 消息也被忽略
    const auto body = chatcompletions::buildRequestBody(
        history, QStringLiteral("m"), QString(), false);

    QCOMPARE(body["messages"].size(), 1);
    QVERIFY(body["messages"][0]["role"].get<std::string>() == "user");
}

void TestChatCompletions::carriesToolsInRequest()
{
    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    const auto body = chatcompletions::buildRequestBody({}, QStringLiteral("m"), {}, true,
                                                        registry.toChatCompletionsTools());
    QVERIFY(body["tools"][0]["function"]["name"].get<std::string>() == "read");

    // 空工具集不携带 tools 字段
    const auto empty = chatcompletions::buildRequestBody({}, QStringLiteral("m"), {}, true,
                                                         nlohmann::json::array());
    QVERIFY(!empty.contains("tools"));
}

void TestChatCompletions::extractsDeltaText()
{
    const auto withContent = nlohmann::json::parse(R"({"choices":[{"delta":{"content":"he"}}]})");
    const auto withoutContent = nlohmann::json::parse(R"({"choices":[{"delta":{}}]})");
    const auto nullContent = nlohmann::json::parse(R"({"choices":[{"delta":{"content":null}}]})");
    QCOMPARE(chatcompletions::extractDeltaText(withContent), QStringLiteral("he"));
    // content 缺失 / null（如 tool_call 增量帧）返回空串
    QCOMPARE(chatcompletions::extractDeltaText(withoutContent), QString());
    QCOMPARE(chatcompletions::extractDeltaText(nullContent), QString());
}

void TestChatCompletions::toleratesMalformedPayload()
{
    const auto discarded = nlohmann::json::parse("{not json", nullptr, false);
    QCOMPARE(chatcompletions::extractDeltaText(discarded), QString());
    QCOMPARE(chatcompletions::extractDeltaText(nlohmann::json::object()), QString());
}

void TestChatCompletions::streamAccumulatesContentAndToolCalls()
{
    const auto chunkHello1 = nlohmann::json::parse(R"({"choices":[{"delta":{"content":"Hel"}}]})");
    const auto chunkHello2 = nlohmann::json::parse(R"({"choices":[{"delta":{"content":"lo"}}]})");
    const auto chunkCallStart = nlohmann::json::parse(
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"c1","function":{"name":"edit","arguments":"{\"pa"}}]}}]})");
    const auto chunkCallArgs = nlohmann::json::parse(
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"th\":\"x\"}"}}]}}]})");
    const auto chunkFinish = nlohmann::json::parse(
        R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})");

    chatcompletions::ChatCompletionStream stream;
    QCOMPARE(stream.apply(chunkHello1).content, QStringLiteral("Hel"));
    QCOMPARE(stream.apply(chunkHello2).content, QStringLiteral("lo"));
    QCOMPARE(stream.apply(chunkCallStart).content, QString());
    QCOMPARE(stream.apply(chunkCallArgs).content, QString());
    QCOMPARE(stream.apply(chunkFinish).content, QString());

    QCOMPARE(stream.content(), QStringLiteral("Hello"));
    const auto calls = stream.toolCalls();
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls[0].id, QStringLiteral("c1"));
    QCOMPARE(calls[0].name, QStringLiteral("edit"));
    QCOMPARE(calls[0].arguments, QStringLiteral("{\"path\":\"x\"}"));
    QCOMPARE(stream.finishReason(), QStringLiteral("tool_calls"));
    QVERIFY(!stream.isDone());

    stream.markDone();
    QVERIFY(stream.isDone());
}

void TestChatCompletions::llamaCppStreamWithReasoning()
{
    const auto first = nlohmann::json::parse(
        R"({"choices":[{"finish_reason":null,"index":0,"delta":{"role":"assistant","content":null}}]})");
    const auto r1 = nlohmann::json::parse(
        R"({"choices":[{"index":0,"delta":{"reasoning_content":"思考"}}]})");
    const auto r2 = nlohmann::json::parse(
        R"({"choices":[{"index":0,"delta":{"reasoning_content":"中"}}]})");
    const auto c1 = nlohmann::json::parse(
        R"({"choices":[{"index":0,"delta":{"content":"答"}}]})");
    const auto c2 = nlohmann::json::parse(
        R"({"choices":[{"index":0,"delta":{"content":"案"}}]})");
    const auto finish = nlohmann::json::parse(
        R"({"choices":[{"finish_reason":"stop","index":0,"delta":{}}],"timings":{"predicted_n":358}})");

    chatcompletions::ChatCompletionStream stream;
    QVERIFY(stream.apply(first).content.isEmpty());
    QCOMPARE(stream.apply(r1).reasoning, QStringLiteral("思考"));
    QCOMPARE(stream.apply(r2).reasoning, QStringLiteral("中"));
    QCOMPARE(stream.apply(c1).content, QStringLiteral("答"));
    QCOMPARE(stream.apply(c2).content, QStringLiteral("案"));
    QCOMPARE(stream.apply(finish).content, QString());

    QCOMPARE(stream.content(), QStringLiteral("答案"));
    QCOMPARE(stream.reasoning(), QStringLiteral("思考中"));
    QCOMPARE(stream.finishReason(), QStringLiteral("stop"));
    QVERIFY(stream.toolCalls().isEmpty());
}

void TestChatCompletions::streamParsesUsageFromFinalChunk()
{
    // OpenAI stream_options.include_usage 形态：末帧无 choices，只有 usage
    chatcompletions::ChatCompletionStream stream;
    stream.apply(nlohmann::json::parse(R"({"choices":[{"delta":{"content":"hi"}}]})"));
    const auto usageOnly = nlohmann::json::parse(
        R"({"usage":{"prompt_tokens":120,"completion_tokens":34,"total_tokens":154,)"
        R"("prompt_tokens_details":{"cached_tokens":64}}})");
    const auto delta = stream.apply(usageOnly);
    QVERIFY(delta.content.isEmpty());
    QVERIFY(stream.usage().valid);
    QCOMPARE(stream.usage().promptTokens, 120);
    QCOMPARE(stream.usage().completionTokens, 34);
    QCOMPARE(stream.usage().cachedTokens, 64);

    // llama.cpp 形态：usage 与 choices 同在末帧
    chatcompletions::ChatCompletionStream stream2;
    stream2.apply(nlohmann::json::parse(
        R"({"choices":[{"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":7,"completion_tokens":3}})"));
    QVERIFY(stream2.usage().valid);
    QCOMPARE(stream2.usage().promptTokens, 7);
    QCOMPARE(stream2.usage().completionTokens, 3);
    QCOMPARE(stream2.usage().cachedTokens, 0);

    // 缺字段 / 非对象 usage 不产生有效用量
    chatcompletions::ChatCompletionStream stream3;
    stream3.apply(nlohmann::json::parse(R"({"usage":{"total_tokens":10}})"));
    QVERIFY(!stream3.usage().valid);
}

void TestChatCompletions::sseParserHandlesSplitChunks()
{
    SseParser parser;
    QList<QByteArray> events;
    const auto collect = [&events](const QByteArray &data) { events.append(data); };

    // 一个事件被切成三段 + CRLF + 注释行
    parser.feed("data: {\"a\"", collect);
    parser.feed("\r\n", collect);
    parser.feed(": keepalive\r\n\r\ndata: [DONE]\n\n", collect);

    QCOMPARE(events.size(), 2);
    QCOMPARE(events[0], QByteArray("{\"a\""));
    QCOMPARE(events[1], QByteArray("[DONE]"));
}

void TestChatCompletions::promptAssemblerOverridesSections()
{
    PromptAssembler assembler;
    assembler.setSection(QStringLiteral("identity"), QStringLiteral("be brief"));
    assembler.setSection(QStringLiteral("workdir"), QStringLiteral("/tmp"));
    assembler.setSection(QStringLiteral("identity"), QStringLiteral("be precise"));
    assembler.setSection(QStringLiteral("empty"), QStringLiteral("   "));

    QCOMPARE(assembler.assemble(),
             QStringLiteral("be precise\n\n/tmp"));
    QCOMPARE(assembler.sectionNames().size(), 3); // 空段落保留名字，供透明化展示
    QCOMPARE(assembler.sectionNames()[0], QStringLiteral("identity")); // 顺序不变
}

void TestChatCompletions::toolRegistryEmitsFunctionFormat()
{
    ToolRegistry registry;
    registry.registerTool(std::make_shared<ReadTool>());
    registry.registerTool(std::make_shared<BashTool>());

    const auto tools = registry.toChatCompletionsTools();
    QCOMPARE(tools.size(), 2);
    QVERIFY(tools[0]["type"].get<std::string>() == "function");
    QVERIFY(tools[0]["function"]["name"].get<std::string>() == "read");
    QVERIFY(tools[1]["function"]["parameters"].is_object());
}

QTEST_GUILESS_MAIN(TestChatCompletions)
#include "test_chat_completions.moc"
