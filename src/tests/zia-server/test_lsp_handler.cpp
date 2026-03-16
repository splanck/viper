//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia-server/test_lsp_handler.cpp
// Purpose: Integration tests for LSP protocol handler.
// Key invariants:
//   - Tests exercise the LSP lifecycle: initialize → didOpen → features → shutdown
//   - A mock transport captures outgoing notifications (publishDiagnostics)
//   - Feature requests validate response structure against LSP 3.17 spec
// Ownership/Lifetime:
//   - Test-only file
// Links: tools/zia-server/LspHandler.hpp, tools/zia-server/DocumentStore.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include "tools/lsp-common/DocumentStore.hpp"
#include "tools/lsp-common/Json.hpp"
#include "tools/lsp-common/JsonRpc.hpp"
#include "tools/lsp-common/LspHandler.hpp"
#include "tools/lsp-common/Transport.hpp"
#include "tools/zia-server/CompilerBridge.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace viper::server;

// --- Mock transport that captures written messages ---

class MockTransport : public Transport
{
  public:
    std::vector<std::string> written;

    bool readMessage(RawMessage & /*msg*/) override
    {
        return false;
    }

    void writeMessage(const std::string &message) override
    {
        written.push_back(message);
    }
};

/// Helper: build a JsonRpcRequest.
static JsonRpcRequest makeReq(const std::string &method,
                              JsonValue params = JsonValue::object({}),
                              JsonValue id = JsonValue(1))
{
    return {method, std::move(params), std::move(id)};
}

/// Helper: build a notification (null id).
static JsonRpcRequest makeNotif(const std::string &method, JsonValue params = JsonValue::object({}))
{
    return {method, std::move(params), JsonValue()};
}

/// Helper: parse response.
static JsonValue parseResponse(const std::string &resp)
{
    EXPECT_TRUE(!resp.empty());
    return JsonValue::parse(resp);
}

/// Standard valid Zia source for testing.
static const char *kValidSource =
    "module Test;\nfunc start() {\n    var x = 42;\n    Viper.Terminal.SayInt(x);\n}\n";

// ===== Lifecycle =====

TEST(LspHandler, Initialize)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    auto resp = parseResponse(handler.handleRequest(makeReq("initialize")));
    EXPECT_EQ(resp["jsonrpc"].asString(), "2.0");

    auto caps = resp["result"]["capabilities"];
    EXPECT_EQ(caps["textDocumentSync"].asInt(), 1);
    EXPECT_TRUE(caps["hoverProvider"].asBool());
    EXPECT_TRUE(caps["documentSymbolProvider"].asBool());
    EXPECT_TRUE(caps.has("completionProvider"));

    auto info = resp["result"]["serverInfo"];
    EXPECT_EQ(info["name"].asString(), "zia-server");
}

TEST(LspHandler, InitializedNotification)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    auto resp = handler.handleRequest(makeNotif("initialized"));
    EXPECT_TRUE(resp.empty());
}

TEST(LspHandler, Shutdown)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    auto resp = parseResponse(handler.handleRequest(makeReq("shutdown")));
    EXPECT_EQ(resp["id"].asInt(), 1);
    // result should be null
    EXPECT_TRUE(resp["result"].isNull());
}

TEST(LspHandler, UnknownMethod)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    auto resp = parseResponse(handler.handleRequest(makeReq("nonexistent/method")));
    EXPECT_TRUE(resp.has("error"));
}

// ===== Document Sync =====

TEST(LspHandler, DidOpenPublishesDiagnostics)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    auto params = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(1)},
             {"text", JsonValue(kValidSource)},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(params)));

    // Should have published diagnostics
    EXPECT_TRUE(transport.written.size() > 0u);
    auto diag = JsonValue::parse(transport.written[0]);
    EXPECT_EQ(diag["method"].asString(), "textDocument/publishDiagnostics");
    EXPECT_EQ(diag["params"]["uri"].asString(), "file:///test.zia");
}

TEST(LspHandler, DidChangeUpdatesDiagnostics)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    // First open the document
    auto openParams = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(1)},
             {"text", JsonValue(kValidSource)},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(openParams)));

    // Clear captured messages
    transport.written.clear();

    // Now change the document to have an error
    auto changeParams = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"version", JsonValue(2)},
         })},
        {"contentChanges",
         JsonValue::array({JsonValue::object(
             {{"text",
               JsonValue("module Test;\nfunc start() {\n    var x = unknownIdent;\n}\n")}})})},
    });
    handler.handleRequest(makeNotif("textDocument/didChange", std::move(changeParams)));

    EXPECT_TRUE(transport.written.size() > 0u);
    auto diag = JsonValue::parse(transport.written[0]);
    // Should have error diagnostics
    EXPECT_TRUE(diag["params"]["diagnostics"].size() > 0u);
}

TEST(LspHandler, DidCloseClearsDiagnostics)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    // Open
    auto openParams = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(1)},
             {"text", JsonValue(kValidSource)},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(openParams)));
    transport.written.clear();

    // Close
    auto closeParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
    });
    handler.handleRequest(makeNotif("textDocument/didClose", std::move(closeParams)));

    // Should publish empty diagnostics
    EXPECT_TRUE(transport.written.size() > 0u);
    auto diag = JsonValue::parse(transport.written[0]);
    EXPECT_EQ(diag["params"]["diagnostics"].size(), 0u);
}

// ===== Completion =====

TEST(LspHandler, CompletionAfterDot)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    // Open document with "Viper." to trigger completions
    std::string source = "module Test;\nfunc start() {\n    Viper.\n}\n";
    auto openParams = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(1)},
             {"text", JsonValue(source)},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(openParams)));

    // Request completions at line 2 (0-based), character 10 (after "Viper.")
    auto compParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
        {"position", JsonValue::object({{"line", JsonValue(2)}, {"character", JsonValue(10)}})},
    });
    auto resp = parseResponse(
        handler.handleRequest(makeReq("textDocument/completion", std::move(compParams))));

    // Result should be an array of completion items
    auto result = resp["result"];
    // Should have items (runtime classes/modules after "Viper.")
    EXPECT_TRUE(result.size() > 0u);
}

TEST(LspHandler, CompletionOnClosedDoc)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    // Request completions without opening — should return empty
    auto compParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///nonexistent.zia")}})},
        {"position", JsonValue::object({{"line", JsonValue(0)}, {"character", JsonValue(0)}})},
    });
    auto resp = parseResponse(
        handler.handleRequest(makeReq("textDocument/completion", std::move(compParams))));
    EXPECT_EQ(resp["result"].size(), 0u);
}

// ===== Hover =====

TEST(LspHandler, HoverOnClosedDoc)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    auto hoverParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///nonexistent.zia")}})},
        {"position", JsonValue::object({{"line", JsonValue(0)}, {"character", JsonValue(0)}})},
    });
    auto resp =
        parseResponse(handler.handleRequest(makeReq("textDocument/hover", std::move(hoverParams))));
    // Should return null result
    EXPECT_TRUE(resp["result"].isNull());
}

TEST(LspHandler, HoverReturnsMarkdownContent)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    std::string source = "module Test;\nfunc start() {\n    var x = 42;\n}\n";
    auto openParams = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(1)},
             {"text", JsonValue(source)},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(openParams)));

    // LSP uses 0-based line/character. Line 1 char 5 = "start" in "func start() {"
    auto hoverParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
        {"position", JsonValue::object({{"line", JsonValue(1)}, {"character", JsonValue(5)}})},
    });
    auto resp =
        parseResponse(handler.handleRequest(makeReq("textDocument/hover", std::move(hoverParams))));

    EXPECT_FALSE(resp["result"].isNull());
    auto contents = resp["result"]["contents"];
    EXPECT_EQ(contents["kind"].asString(), "markdown");
    EXPECT_TRUE(contents["value"].asString().find("func start") != std::string::npos);
}

TEST(LspHandler, HoverOnLocalVariableViaLsp)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    std::string source = "module Test;\nfunc start() {\n    var x = 42;\n}\n";
    auto openParams = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(1)},
             {"text", JsonValue(source)},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(openParams)));

    // LSP 0-based: line 2, character 8 = 'x' in "    var x = 42;"
    auto hoverParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
        {"position", JsonValue::object({{"line", JsonValue(2)}, {"character", JsonValue(8)}})},
    });
    auto resp =
        parseResponse(handler.handleRequest(makeReq("textDocument/hover", std::move(hoverParams))));

    EXPECT_FALSE(resp["result"].isNull());
    auto value = resp["result"]["contents"]["value"].asString();
    EXPECT_TRUE(value.find("var x") != std::string::npos);
    EXPECT_TRUE(value.find("Integer") != std::string::npos);
}

TEST(LspHandler, HoverOnWhitespaceReturnsNull)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    std::string source = "module Test;\nfunc start() {\n    var x = 42;\n}\n";
    auto openParams = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(1)},
             {"text", JsonValue(source)},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(openParams)));

    // LSP 0-based: line 2, character 0 = leading whitespace
    auto hoverParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
        {"position", JsonValue::object({{"line", JsonValue(2)}, {"character", JsonValue(0)}})},
    });
    auto resp =
        parseResponse(handler.handleRequest(makeReq("textDocument/hover", std::move(hoverParams))));

    EXPECT_TRUE(resp["result"].isNull());
}

// ===== Document Symbol =====

TEST(LspHandler, DocumentSymbolsListsFunctions)
{
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    // Open document
    auto openParams = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(1)},
             {"text", JsonValue(kValidSource)},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(openParams)));

    auto symParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
    });
    auto resp = parseResponse(
        handler.handleRequest(makeReq("textDocument/documentSymbol", std::move(symParams))));

    auto result = resp["result"];
    // Should contain at least "start" function
    bool foundStart = false;
    for (size_t i = 0; i < result.size(); ++i)
    {
        if (result.at(i)["name"].asString() == "start")
            foundStart = true;
    }
    EXPECT_TRUE(foundStart);
}

// ===== DocumentStore =====

TEST(DocumentStore, UriToPathUnix)
{
    auto path = DocumentStore::uriToPath("file:///Users/test/file.zia");
    EXPECT_EQ(path, "/Users/test/file.zia");
}

TEST(DocumentStore, UriToPathWindowsDrive)
{
    auto path = DocumentStore::uriToPath("file:///C:/Users/test/file.zia");
    EXPECT_EQ(path, "C:/Users/test/file.zia");
}

TEST(DocumentStore, UriToPathPercentDecode)
{
    auto path = DocumentStore::uriToPath("file:///path/my%20file.zia");
    EXPECT_EQ(path, "/path/my file.zia");
}

TEST(DocumentStore, UriToPathPlainPath)
{
    auto path = DocumentStore::uriToPath("/just/a/path.zia");
    EXPECT_EQ(path, "/just/a/path.zia");
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
