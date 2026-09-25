#include <QtTest/QtTest>

#include <lens/core/providers/ProtocolAdapter.hpp>
#include <lens/core/providers/AnthropicClient.hpp>
#include <lens/core/providers/ResponsesClient.hpp>

using namespace lens;

namespace {

ToolSpec makeToolSpec()
{
    return {QStringLiteral("write"), QStringLiteral("写入文件"),
            nlohmann::json{{"type", "object"},
                           {"properties", {{"path", {{"type", "string"}}}}},
                           {"required", nlohmann::json::array({"path"})}}};
}

Message userMessage(const QString &content)
{
    Message message;
    message.role = Role::User;
    message.content = content;
    return message;
}

} // namespace

class TestProtocols : public QObject
{
    Q_OBJECT

private slots:
    // —— 端点解析：完整端点原样用，根路径自动补全 ——

    void chatEndpointResolution();
    void anthropicEndpointResolution();
    void responsesEndpointResolution();

    // —— Anthropic ——

    void anthropicRequestShape();
    void anthropicToolResultMergesIntoUserMessage();
    void anthropicEventStream();
    void anthropicServerToolUseIsNotLocalToolCall();
    void anthropicErrorEvent();

    // —— Responses ——

    void responsesRequestShape();
    void responsesEventStream();
    void responsesErrorEvent();

    // —— 工厂与协议名 ——

    void factoryAndProtocolNames();

    // —— 服务端搜索（RequestFeatures::serverSideSearch） ——

    void serverSideSearchRequestShape();
    void serverSideSearchOffByDefault();
};

void TestProtocols::chatEndpointResolution()
{
    const auto adapter = makeProtocolAdapter(Protocol::ChatCompletions);
    QCOMPARE(adapter->resolveEndpoint(QStringLiteral("https://api.openai.com/v1/chat/completions"))
                 .toString(),
             QStringLiteral("https://api.openai.com/v1/chat/completions"));
    QCOMPARE(adapter->resolveEndpoint(QStringLiteral("https://api.openai.com/v1/")).toString(),
             QStringLiteral("https://api.openai.com/v1/chat/completions"));
}

void TestProtocols::anthropicEndpointResolution()
{
    const anthropic::AnthropicAdapter adapter;
    QCOMPARE(adapter.resolveEndpoint(QStringLiteral("https://api.anthropic.com")).toString(),
             QStringLiteral("https://api.anthropic.com/v1/messages"));
    // TODO: 以 /v1 结尾的 base 只补 /messages，避免 /v1/v1/messages
    QCOMPARE(adapter.resolveEndpoint(QStringLiteral("https://proxy.example/anthropic")).toString(),
             QStringLiteral("https://proxy.example/anthropic/v1/messages"));
    // 完整端点原样使用
    QCOMPARE(adapter.resolveEndpoint(QStringLiteral("https://proxy.example/v1/messages")).toString(),
             QStringLiteral("https://proxy.example/v1/messages"));
}

void TestProtocols::responsesEndpointResolution()
{
    const responses::ResponsesAdapter adapter;
    QCOMPARE(adapter.resolveEndpoint(QStringLiteral("https://api.openai.com/v1")).toString(),
             QStringLiteral("https://api.openai.com/v1/responses"));
    QCOMPARE(adapter.resolveEndpoint(QStringLiteral("https://api.openai.com/v1/responses")).toString(),
             QStringLiteral("https://api.openai.com/v1/responses"));
}

void TestProtocols::anthropicRequestShape()
{
    const anthropic::AnthropicAdapter adapter;
    const nlohmann::json body = adapter.buildRequestBody(
        {userMessage(QStringLiteral("你好"))}, QStringLiteral("claude-sonnet-4"),
        QStringLiteral("系统提示词"), true, {makeToolSpec()});

    QCOMPARE(body.at("model").get<std::string>(), "claude-sonnet-4");
    QCOMPARE(body.at("system").get<std::string>(), "系统提示词");
    QVERIFY(body.contains("max_tokens"));
    QCOMPARE(body.at("stream").get<bool>(), true);

    const auto &messages = body.at("messages");
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages[0].at("role").get<std::string>(), "user");
    QCOMPARE(messages[0].at("content")[0].at("type").get<std::string>(), "text");
    QCOMPARE(messages[0].at("content")[0].at("text").get<std::string>(), "你好");

    const auto &tools = body.at("tools");
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools[0].at("name").get<std::string>(), "write");
    QCOMPARE(tools[0].at("input_schema").at("type").get<std::string>(), "object");

    // 鉴权头
    const auto headers = adapter.extraHeaders(QStringLiteral("sk-key"));
    bool hasApiKey = false, hasVersion = false;
    for (const auto &[key, value] : headers) {
        if (key == "x-api-key" && value == "sk-key")
            hasApiKey = true;
        if (key == "anthropic-version")
            hasVersion = true;
    }
    QVERIFY(hasApiKey && hasVersion);
}

void TestProtocols::anthropicToolResultMergesIntoUserMessage()
{
    // assistant(tool_use) + 两条连续 Tool 结果 → 一条 user 消息带两个 tool_result 块
    Message assistant;
    assistant.role = Role::Assistant;
    assistant.reasoning = QStringLiteral("先写入再执行");
    assistant.toolCalls.append({QStringLiteral("tu_1"), QStringLiteral("write"),
                                QStringLiteral("{\"path\":\"a.txt\"}")});
    assistant.toolCalls.append({QStringLiteral("tu_2"), QStringLiteral("bash"), QStringLiteral("{}")});
    Message toolResult1;
    toolResult1.role = Role::Tool;
    toolResult1.toolCallId = QStringLiteral("tu_1");
    toolResult1.content = QStringLiteral("已写入");
    Message toolResult2;
    toolResult2.role = Role::Tool;
    toolResult2.toolCallId = QStringLiteral("tu_2");
    toolResult2.content = QStringLiteral("ok");

    const anthropic::AnthropicAdapter adapter;
    const nlohmann::json body =
        adapter.buildRequestBody({userMessage("hi"), assistant, toolResult1, toolResult2},
                                 QStringLiteral("m"), QString(), true, {});

    const auto &messages = body.at("messages");
    QCOMPARE(messages.size(), 3); // user / assistant / user(tool_result×2)
    const auto &toolResults = messages[2].at("content");
    QCOMPARE(toolResults.size(), 2);
    QCOMPARE(toolResults[0].at("type").get<std::string>(), "tool_result");
    QCOMPARE(toolResults[0].at("tool_use_id").get<std::string>(), "tu_1");
    QCOMPARE(toolResults[1].at("tool_use_id").get<std::string>(), "tu_2");

    // assistant 的 tool_use 块：arguments 解析为 input 对象；thinking 块必须回传
    // （Anthropic 扩展思考 / DeepSeek 兼容层在工具轮缺 thinking 块时 400）
    const auto &blocks = messages[1].at("content");
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[0].at("type").get<std::string>(), "thinking");
    QCOMPARE(blocks[1].at("type").get<std::string>(), "tool_use");
    QCOMPARE(blocks[1].at("input").at("path").get<std::string>(), "a.txt");
    QCOMPARE(blocks[2].at("input"), nlohmann::json::object());
}

void TestProtocols::anthropicEventStream()
{
    const anthropic::AnthropicAdapter adapter;
    chatcompletions::ChatCompletionStream stream;

    const auto feed = [&](const QByteArray &payload) {
        const auto json = nlohmann::json::parse(payload.constData(),
                                                payload.constData() + payload.size(), nullptr,
                                                false);
        adapter.applyEvent(json, stream);
    };

    // 文本增量
    feed(R"({"type":"content_block_start","index":0,"content_block":{"type":"text"}})");
    feed(R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"已写入"}})");
    feed(R"({"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"推理"}})");
    // 工具调用：块起点 + 参数分片
    feed(R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"tu_9","name":"write"}})");
    feed(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"path\":"}})");
    feed(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"\"b.txt\"}"}})");
    feed(R"({"type":"message_delta","delta":{"stop_reason":"tool_use"}})");
    feed(R"({"type":"message_stop"})");

    QCOMPARE(stream.content(), QStringLiteral("已写入"));
    QCOMPARE(stream.reasoning(), QStringLiteral("推理"));
    QCOMPARE(stream.finishReason(), QStringLiteral("tool_calls"));
    QVERIFY(stream.isDone());

    const auto calls = stream.toolCalls();
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.first().id, QStringLiteral("tu_9"));
    QCOMPARE(calls.first().name, QStringLiteral("write"));
    QCOMPARE(calls.first().arguments, QStringLiteral("{\"path\":\"b.txt\"}"));

    // ping 等无关事件被忽略
    chatcompletions::ChatCompletionStream quiet;
    const auto json = nlohmann::json::parse(R"({"type":"ping"})", nullptr, false);
    const auto delta = adapter.applyEvent(json, quiet);
    QVERIFY(delta.content.isEmpty());
    QVERIFY(!quiet.isDone());
}

void TestProtocols::anthropicServerToolUseIsNotLocalToolCall()
{
    // 服务端工具（如 web_search_20250305）的 server_tool_use 块也用 input_json_delta
    // 流式下发输入，但不是本地工具调用：不得进入 toolCalls，否则会把无效的
    // 空 tool_use 回灌给服务端（DeepSeek 实测会 400）。
    anthropic::AnthropicAdapter adapter;
    chatcompletions::ChatCompletionStream stream;
    const auto feed = [&](const char *payload) {
        const auto json = nlohmann::json::parse(payload, payload + strlen(payload), nullptr, false);
        adapter.applyEvent(json, stream);
    };

    feed(R"({"type":"message_start","message":{"id":"m1","role":"assistant"}})");
    feed(R"({"type":"content_block_start","index":0,"content_block":{"type":"thinking","thinking":""}})");
    feed(R"({"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"搜一下"}})");
    feed(R"({"type":"content_block_stop","index":0})");
    feed(R"({"type":"content_block_start","index":1,"content_block":{"type":"server_tool_use","id":"call_1","name":"web_search","input":{}}})");
    feed(R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"query\":\"AI news\"}"}})");
    feed(R"({"type":"content_block_stop","index":1})");
    feed(R"({"type":"content_block_start","index":2,"content_block":{"type":"web_search_tool_result","tool_use_id":"call_1","content":[]}})");
    feed(R"({"type":"content_block_stop","index":2})");
    feed(R"({"type":"content_block_start","index":3,"content_block":{"type":"text"}})");
    feed(R"({"type":"content_block_delta","index":3,"delta":{"type":"text_delta","text":"今天的新闻是……"}})");
    feed(R"({"type":"content_block_delta","index":3,"delta":{"type":"text_delta","text":"来源：路透社"}})");
    feed(R"({"type":"message_delta","delta":{"stop_reason":"end_turn"}})");
    feed(R"({"type":"message_stop"})");

    QVERIFY(stream.toolCalls().isEmpty()); // 关键断言：服务端工具不产生本地工具调用
    QCOMPARE(stream.content(), QStringLiteral("今天的新闻是……来源：路透社"));
    QCOMPARE(stream.reasoning(), QStringLiteral("搜一下"));
    QVERIFY(stream.isDone());

    // 新回合 message_start 清空状态：真正的 tool_use 依然正常累积
    feed(R"({"type":"message_start","message":{"id":"m2","role":"assistant"}})");
    feed(R"({"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"tu_1","name":"write"}})");
    feed(R"({"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"path\":\"a\"}"}})");
    const auto calls = stream.toolCalls();
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.first().id, QStringLiteral("tu_1"));
    QCOMPARE(calls.first().arguments, QStringLiteral("{\"path\":\"a\"}"));
}

void TestProtocols::anthropicErrorEvent()
{
    const anthropic::AnthropicAdapter adapter;
    const auto json = nlohmann::json::parse(
        R"({"type":"error","error":{"type":"overloaded_error","message":"过载"}})", nullptr, false);
    QCOMPARE(adapter.errorFromEvent(json), QStringLiteral("过载"));
    QVERIFY(adapter
                .errorFromEvent(nlohmann::json::parse(R"({"type":"content_block_delta"})", nullptr,
                                                      false))
                .isEmpty());
}

void TestProtocols::responsesRequestShape()
{
    const responses::ResponsesAdapter adapter;
    const nlohmann::json body =
        adapter.buildRequestBody({userMessage(QStringLiteral("查一下"))},
                                 QStringLiteral("gpt-5"), QStringLiteral("系统提示词"), true,
                                 {makeToolSpec()});

    QCOMPARE(body.at("model").get<std::string>(), "gpt-5");
    QCOMPARE(body.at("instructions").get<std::string>(), "系统提示词");
    QCOMPARE(body.at("stream").get<bool>(), true);
    QCOMPARE(body.at("store").get<bool>(), false);

    const auto &items = body.at("input");
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].at("type").get<std::string>(), "message");
    QCOMPARE(items[0].at("role").get<std::string>(), "user");
    QCOMPARE(items[0].at("content")[0].at("type").get<std::string>(), "input_text");

    const auto &tools = body.at("tools");
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools[0].at("type").get<std::string>(), "function");
    QCOMPARE(tools[0].at("name").get<std::string>(), "write");
    QVERIFY(tools[0].contains("parameters"));
    QVERIFY(!body.at("tools")[0].contains("input_schema"));

    const auto headers = adapter.extraHeaders(QStringLiteral("sk-key"));
    QCOMPARE(headers.first().first, QByteArrayLiteral("Authorization"));
    QVERIFY(headers.first().second.contains("sk-key"));
}

void TestProtocols::responsesEventStream()
{
    const responses::ResponsesAdapter adapter;
    chatcompletions::ChatCompletionStream stream;
    const auto feed = [&](const char *payload) {
        const auto json = nlohmann::json::parse(payload, payload + strlen(payload), nullptr, false);
        adapter.applyEvent(json, stream);
    };

    feed(R"({"type":"response.output_text.delta","delta":"正在"})");
    feed(R"({"type":"response.reasoning_text.delta","delta":"思考"})");
    feed(R"({"type":"response.output_item.added","output_index":1,"item":{"type":"function_call","call_id":"fc_1","name":"write"}})");
    feed(R"({"type":"response.function_call_arguments.delta","output_index":1,"delta":"{\"path\""})");
    feed(R"({"type":"response.function_call_arguments.delta","output_index":1,"delta":":\"c.txt\"}"})");
    feed(R"({"type":"response.completed","response":{"id":"resp_1"}})");

    QCOMPARE(stream.content(), QStringLiteral("正在"));
    QCOMPARE(stream.reasoning(), QStringLiteral("思考"));
    QCOMPARE(stream.finishReason(), QStringLiteral("stop"));
    QVERIFY(stream.isDone());

    const auto calls = stream.toolCalls();
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.first().id, QStringLiteral("fc_1"));
    QCOMPARE(calls.first().arguments, QStringLiteral("{\"path\":\"c.txt\"}"));
}

void TestProtocols::responsesErrorEvent()
{
    const responses::ResponsesAdapter adapter;
    const auto failed = nlohmann::json::parse(
        R"({"type":"response.failed","response":{"error":{"message":"配额不足"}}})", nullptr, false);
    QCOMPARE(adapter.errorFromEvent(failed), QStringLiteral("配额不足"));
    const auto plainError =
        nlohmann::json::parse(R"({"type":"error","message":"服务器错误"})", nullptr, false);
    QCOMPARE(adapter.errorFromEvent(plainError), QStringLiteral("服务器错误"));
    QVERIFY(adapter
                .errorFromEvent(
                    nlohmann::json::parse(R"({"type":"response.output_text.delta"})", nullptr, false))
                .isEmpty());
}

void TestProtocols::serverSideSearchRequestShape()
{
    const std::vector<Message> history = {userMessage(QStringLiteral("搜一下最新消息"))};

    // chat completions → web_search_options
    const auto chat = makeProtocolAdapter(Protocol::ChatCompletions);
    RequestFeatures on;
    on.serverSideSearch = true;
    const nlohmann::json chatBody =
        chat->buildRequestBody(history, QStringLiteral("m"), QString(), true, {}, on);
    QVERIFY(chatBody.contains("web_search_options"));
    QVERIFY(chatBody.at("web_search_options").is_object());
    QVERIFY(!chat->buildRequestBody(history, QStringLiteral("m"), QString(), true, {}, {})
                 .contains("web_search_options"));

    // anthropic → web_search_20250305 服务端工具，与自定义工具共存
    const anthropic::AnthropicAdapter anthropic;
    const nlohmann::json anthropicBody = anthropic.buildRequestBody(
        history, QStringLiteral("m"), QString(), true, {makeToolSpec()}, on);
    const auto &anthropicTools = anthropicBody.at("tools");
    QCOMPARE(anthropicTools.size(), 2);
    QCOMPARE(anthropicTools[0].at("name").get<std::string>(), "write");
    QCOMPARE(anthropicTools[1].at("type").get<std::string>(), "web_search_20250305");
    QCOMPARE(anthropicTools[1].at("name").get<std::string>(), "web_search");
    // 未开启时不出现
    QVERIFY(!anthropic
                 .buildRequestBody(history, QStringLiteral("m"), QString(), true, {},
                                   RequestFeatures{})
                 .contains("tools"));

    // responses → {"type":"web_search"} 内建工具
    const responses::ResponsesAdapter responses;
    const nlohmann::json responsesBody = responses.buildRequestBody(
        history, QStringLiteral("m"), QString(), true, {makeToolSpec()}, on);
    const auto &responseTools = responsesBody.at("tools");
    QCOMPARE(responseTools.size(), 2);
    QCOMPARE(responseTools[1].at("type").get<std::string>(), "web_search");
    QVERIFY(!responses
                 .buildRequestBody(history, QStringLiteral("m"), QString(), true, {},
                                   RequestFeatures{})
                 .contains("tools"));
}

void TestProtocols::serverSideSearchOffByDefault()
{
    // 默认构造的 RequestFeatures 不改变任何协议的请求体
    const auto adapter = makeProtocolAdapter(Protocol::ChatCompletions);
    const nlohmann::json body = adapter->buildRequestBody(
        {userMessage(QStringLiteral("hi"))}, QStringLiteral("m"), QString(), true, {});
    QVERIFY(!body.contains("web_search_options"));
    QVERIFY(!body.contains("tools"));
}

void TestProtocols::factoryAndProtocolNames()
{
    QVERIFY(protocolFromString(QStringLiteral("anthropic")).has_value());
    QCOMPARE(*protocolFromString(QStringLiteral("anthropic")), Protocol::Anthropic);
    QCOMPARE(*protocolFromString(QStringLiteral("responses")), Protocol::Responses);
    QCOMPARE(*protocolFromString(QStringLiteral("chat_completions")), Protocol::ChatCompletions);
    QVERIFY(!protocolFromString(QStringLiteral("unknown")).has_value());
    QCOMPARE(protocolToString(Protocol::Anthropic), QStringLiteral("anthropic"));

    // 各协议工厂产物行为可用：请求体都带 model
    for (const Protocol protocol :
         {Protocol::ChatCompletions, Protocol::Responses, Protocol::Anthropic}) {
        const auto adapter = makeProtocolAdapter(protocol);
        const nlohmann::json body = adapter->buildRequestBody(
            {userMessage(QStringLiteral("hi"))}, QStringLiteral("m"), QString(), true, {});
        QCOMPARE(body.at("model").get<std::string>(), "m");
    }
}

QTEST_GUILESS_MAIN(TestProtocols)
#include "test_protocols.moc"
