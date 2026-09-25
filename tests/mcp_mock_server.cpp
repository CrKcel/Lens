// MCP stdio mock 服务器：逐行读 JSON-RPC 请求并回应。
// 支持 initialize / tools/list / tools/call（echo、fail 两个工具），仅用于 test_mcp。
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto frame = json::parse(line, nullptr, false);
        if (frame.is_discarded() || !frame.is_object() || !frame.contains("method"))
            continue;
        const std::string method = frame.value("method", std::string());
        if (method == "notifications/initialized")
            continue;

        json response{{"jsonrpc", "2.0"}, {"id", frame.value("id", 0)}};
        if (method == "initialize") {
            response["result"] = {{"protocolVersion", "2024-11-05"},
                                  {"capabilities", json::object({{"tools", json::object()}})},
                                  {"serverInfo", {{"name", "mock-mcp"}, {"version", "0.1"}}}};
        } else if (method == "tools/list") {
            response["result"] = {{"tools",
                                   json::array({{{"name", "echo"},
                                                 {"description", "回显输入文本"},
                                                 {"inputSchema",
                                                  {{"type", "object"},
                                                   {"properties",
                                                    {{"text", {{"type", "string"}}}}},
                                                   {"required", json::array({"text"})}}}},
                                                {{"name", "fail"},
                                                 {"description", "总是失败"},
                                                 {"inputSchema", json::object()}}})}};
        } else if (method == "tools/call") {
            const std::string name = frame.value("params", json::object()).value("name", "");
            const json arguments = frame.value("params", json::object()).value("arguments",
                                                                               json::object());
            if (name == "echo") {
                response["result"] = {
                    {"content", json::array({{{"type", "text"},
                                              {"text", "echo: " + arguments.value("text", "")}}})},
                    {"isError", false}};
            } else if (name == "fail") {
                response["result"] = {{"content", json::array()},
                                       {"isError", true},
                                       {"error", "mock failure"}};
            } else {
                response["error"] = {{"code", -32601}, {"message", "unknown tool"}};
            }
        } else {
            response["error"] = {{"code", -32601}, {"message", "unknown method"}};
        }
        std::cout << response.dump() << std::endl;
    }
    return 0;
}
