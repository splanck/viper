//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia-server/test_mcp_handler.cpp
// Purpose: Integration tests for MCP protocol handler (lifecycle + tool dispatch).
// Key invariants:
//   - Tests exercise the full MCP lifecycle: initialize → tools/list → tools/call
//   - Tool dispatch is tested against the real CompilerBridge
//   - Response structure matches MCP 2024-11-05 specification
// Ownership/Lifetime:
//   - Test-only file
// Links: tools/zia-server/McpHandler.hpp, tools/zia-server/CompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include "tools/zia-server/CompilerBridge.hpp"
#include "tools/zia-server/Json.hpp"
#include "tools/zia-server/JsonRpc.hpp"
#include "tools/zia-server/McpHandler.hpp"

#include <string>

using namespace viper::server;

/// Helper: build a JsonRpcRequest from method, params, and id.
static JsonRpcRequest makeReq(const std::string &method,
                              JsonValue params = JsonValue::object({}),
                              JsonValue id = JsonValue(1))
{
    return {method, std::move(params), std::move(id)};
}

/// Helper: parse a JSON-RPC response string and return the parsed JSON.
static JsonValue parseResponse(const std::string &resp)
{
    EXPECT_TRUE(!resp.empty());
    return JsonValue::parse(resp);
}

// ===== Lifecycle =====

TEST(McpHandler, Initialize)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto resp = parseResponse(handler.handleRequest(makeReq("initialize")));
    EXPECT_EQ(resp["jsonrpc"].asString(), "2.0");
    EXPECT_EQ(resp["id"].asInt(), 1);

    auto result = resp["result"];
    EXPECT_EQ(result["protocolVersion"].asString(), "2024-11-05");
    EXPECT_EQ(result["serverInfo"]["name"].asString(), "zia-server");
    EXPECT_TRUE(result["capabilities"].has("tools"));
}

TEST(McpHandler, InitializedNotification)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    // initialized is a notification (no id) — should return empty
    auto resp =
        handler.handleRequest({"initialized", JsonValue::object({}), JsonValue() /* null id */});
    EXPECT_TRUE(resp.empty());
}

TEST(McpHandler, Ping)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto resp = parseResponse(handler.handleRequest(makeReq("ping")));
    EXPECT_EQ(resp["id"].asInt(), 1);
    EXPECT_TRUE(resp.has("result"));
}

TEST(McpHandler, UnknownMethod)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto resp = parseResponse(handler.handleRequest(makeReq("nonexistent/method")));
    EXPECT_TRUE(resp.has("error"));
    EXPECT_EQ(resp["error"]["code"].asInt(), kMethodNotFound);
}

TEST(McpHandler, UnknownNotificationSilent)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto resp = handler.handleRequest(
        {"someNotification", JsonValue::object({}), JsonValue() /* null id */});
    EXPECT_TRUE(resp.empty());
}

// ===== tools/list =====

TEST(McpHandler, ToolsListReturnsTools)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto resp = parseResponse(handler.handleRequest(makeReq("tools/list")));
    auto tools = resp["result"]["tools"];
    // Should have 11 tools
    EXPECT_EQ(tools.size(), 11u);
}

TEST(McpHandler, ToolsListContainsCheck)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto resp = parseResponse(handler.handleRequest(makeReq("tools/list")));
    auto tools = resp["result"]["tools"].asArray();

    bool foundCheck = false;
    for (const auto &tool : tools)
    {
        if (tool["name"].asString() == "zia/check")
        {
            foundCheck = true;
            // Should have an inputSchema
            EXPECT_TRUE(tool.has("inputSchema"));
            EXPECT_TRUE(tool.has("description"));
        }
    }
    EXPECT_TRUE(foundCheck);
}

TEST(McpHandler, ToolsListHasAllTools)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto resp = parseResponse(handler.handleRequest(makeReq("tools/list")));
    auto tools = resp["result"]["tools"].asArray();

    std::vector<std::string> expected = {
        "zia/check",
        "zia/compile",
        "zia/completions",
        "zia/hover",
        "zia/symbols",
        "zia/dump-il",
        "zia/dump-ast",
        "zia/dump-tokens",
        "zia/runtime-classes",
        "zia/runtime-methods",
        "zia/runtime-search",
    };

    for (const auto &name : expected)
    {
        bool found = false;
        for (const auto &tool : tools)
        {
            if (tool["name"].asString() == name)
                found = true;
        }
        EXPECT_TRUE(found);
    }
}

// ===== tools/call =====

TEST(McpHandler, ToolsCallCheck)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto params = JsonValue::object({
        {"name", JsonValue("zia/check")},
        {"arguments",
         JsonValue::object({
             {"source",
              JsonValue("module Test;\nfunc start() {\n    var x = 42;\n    "
                        "Viper.Terminal.SayInt(x);\n}\n")},
         })},
    });
    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", std::move(params))));
    EXPECT_TRUE(resp.has("result"));
    auto content = resp["result"]["content"];
    EXPECT_TRUE(content.size() > 0u);
    EXPECT_EQ(content.at(0)["type"].asString(), "text");
}

TEST(McpHandler, ToolsCallCompile)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto params = JsonValue::object({
        {"name", JsonValue("zia/compile")},
        {"arguments",
         JsonValue::object({
             {"source",
              JsonValue("module Test;\nfunc start() {\n    var x = 42;\n    "
                        "Viper.Terminal.SayInt(x);\n}\n")},
         })},
    });
    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", std::move(params))));
    auto text = resp["result"]["content"].at(0)["text"].asString();
    auto parsed = JsonValue::parse(text);
    EXPECT_TRUE(parsed["succeeded"].asBool());
}

TEST(McpHandler, ToolsCallCompletions)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto params = JsonValue::object({
        {"name", JsonValue("zia/completions")},
        {"arguments",
         JsonValue::object({
             {"source", JsonValue("module Test;\nfunc start() {\n    Viper.\n}\n")},
             {"line", JsonValue(3)},
             {"col", JsonValue(11)},
         })},
    });
    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", std::move(params))));
    EXPECT_TRUE(resp.has("result"));
    auto text = resp["result"]["content"].at(0)["text"].asString();
    // Should be a JSON array (possibly empty, but valid JSON)
    auto parsed = JsonValue::parse(text);
    (void)parsed;
}

TEST(McpHandler, ToolsCallSymbols)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto params = JsonValue::object({
        {"name", JsonValue("zia/symbols")},
        {"arguments",
         JsonValue::object({
             {"source",
              JsonValue("module Test;\nfunc start() {\n    var x = 42;\n    "
                        "Viper.Terminal.SayInt(x);\n}\n")},
         })},
    });
    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", std::move(params))));
    auto text = resp["result"]["content"].at(0)["text"].asString();
    auto parsed = JsonValue::parse(text);
    // Should contain at least "start"
    bool foundStart = false;
    for (size_t i = 0; i < parsed.size(); ++i)
    {
        if (parsed.at(i)["name"].asString() == "start")
            foundStart = true;
    }
    EXPECT_TRUE(foundStart);
}

TEST(McpHandler, ToolsCallDumpTokens)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto params = JsonValue::object({
        {"name", JsonValue("zia/dump-tokens")},
        {"arguments",
         JsonValue::object({
             {"source", JsonValue("module Test;\n")},
         })},
    });
    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", std::move(params))));
    auto text = resp["result"]["content"].at(0)["text"].asString();
    EXPECT_TRUE(text.find("module") != std::string::npos);
}

TEST(McpHandler, ToolsCallRuntimeClasses)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto params = JsonValue::object({
        {"name", JsonValue("zia/runtime-classes")},
        {"arguments", JsonValue::object({})},
    });
    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", std::move(params))));
    auto text = resp["result"]["content"].at(0)["text"].asString();
    auto parsed = JsonValue::parse(text);
    EXPECT_TRUE(parsed.size() > 0u);
}

TEST(McpHandler, ToolsCallRuntimeMethods)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto params = JsonValue::object({
        {"name", JsonValue("zia/runtime-methods")},
        {"arguments", JsonValue::object({{"className", JsonValue("Viper.Terminal")}})},
    });
    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", std::move(params))));
    auto text = resp["result"]["content"].at(0)["text"].asString();
    auto parsed = JsonValue::parse(text);
    EXPECT_TRUE(parsed.size() > 0u);
}

TEST(McpHandler, ToolsCallRuntimeSearch)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto params = JsonValue::object({
        {"name", JsonValue("zia/runtime-search")},
        {"arguments", JsonValue::object({{"keyword", JsonValue("Say")}})},
    });
    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", std::move(params))));
    auto text = resp["result"]["content"].at(0)["text"].asString();
    auto parsed = JsonValue::parse(text);
    EXPECT_TRUE(parsed.size() > 0u);
}

TEST(McpHandler, ToolsCallUnknownTool)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto params = JsonValue::object({
        {"name", JsonValue("nonexistent/tool")},
        {"arguments", JsonValue::object({})},
    });
    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", std::move(params))));
    EXPECT_TRUE(resp.has("error"));
}

TEST(McpHandler, ToolsCallMissingName)
{
    CompilerBridge bridge;
    McpHandler handler(bridge);

    auto resp = parseResponse(handler.handleRequest(makeReq("tools/call", JsonValue::object({}))));
    EXPECT_TRUE(resp.has("error"));
    EXPECT_EQ(resp["error"]["code"].asInt(), kInvalidParams);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
