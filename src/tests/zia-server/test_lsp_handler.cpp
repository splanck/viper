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

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace viper::server;

// --- Mock transport that captures written messages ---

class MockTransport : public Transport {
  public:
    std::vector<std::string> written;

    bool readMessage(RawMessage & /*msg*/) override {
        return false;
    }

    void writeMessage(const std::string &message) override {
        written.push_back(message);
    }
};

/// Helper: build a JsonRpcRequest.
static JsonRpcRequest makeReq(const std::string &method,
                              JsonValue params = JsonValue::object({}),
                              JsonValue id = JsonValue(1)) {
    JsonRpcRequest req{method, std::move(params), std::move(id)};
    req.hasId = true;
    return req;
}

/// Helper: build a notification (null id).
static JsonRpcRequest makeNotif(const std::string &method,
                                JsonValue params = JsonValue::object({})) {
    return {method, std::move(params), JsonValue(), false};
}

/// Helper: parse response.
static JsonValue parseResponse(const std::string &resp) {
    EXPECT_TRUE(!resp.empty());
    return JsonValue::parse(resp);
}

/// Helper: drive an LSP handler through initialize and initialized.
/// Feature tests call this before document or request operations so they
/// exercise normal LSP-ready behavior instead of pre-initialization rejection.
static void startLspSession(LspHandler &handler) {
    (void)handler.handleRequest(makeReq("initialize"));
    (void)handler.handleRequest(makeNotif("initialized"));
}

static void openDocument(LspHandler &handler,
                         const std::string &uri,
                         const std::string &source,
                         int version = 1) {
    auto params = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue(uri)},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(version)},
             {"text", JsonValue(source)},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(params)));
}

static JsonValue positionForOffset(const std::string &source, size_t offset) {
    int line = 0;
    int character = 0;
    const size_t limit = std::min(offset, source.size());
    for (size_t i = 0; i < limit; ++i) {
        if (source[i] == '\n') {
            ++line;
            character = 0;
        } else {
            ++character;
        }
    }
    return JsonValue::object({{"line", JsonValue(line)}, {"character", JsonValue(character)}});
}

static JsonValue positionOf(const std::string &source, const std::string &needle) {
    const size_t offset = source.find(needle);
    EXPECT_TRUE(offset != std::string::npos);
    return positionForOffset(source, offset == std::string::npos ? 0 : offset);
}

static JsonValue positionAfter(const std::string &source, const std::string &needle) {
    const size_t offset = source.find(needle);
    EXPECT_TRUE(offset != std::string::npos);
    return positionForOffset(source, offset == std::string::npos ? 0 : offset + needle.size());
}

/// Standard valid Zia source for testing.
static const char *kValidSource =
    "module Test;\nfunc start() {\n    var x = 42;\n    Viper.Terminal.SayInt(x);\n}\n";

// ===== Lifecycle =====

TEST(LspHandler, Initialize) {
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
    EXPECT_TRUE(caps["definitionProvider"].asBool());
    EXPECT_TRUE(caps["referencesProvider"].asBool());
    EXPECT_TRUE(caps["renameProvider"].asBool());
    EXPECT_TRUE(caps.has("signatureHelpProvider"));
    EXPECT_TRUE(caps["workspaceSymbolProvider"].asBool());
    EXPECT_TRUE(caps.has("semanticTokensProvider"));
    EXPECT_TRUE(caps["semanticTokensProvider"]["full"].asBool());
    EXPECT_TRUE(caps["semanticTokensProvider"]["legend"]["tokenTypes"].size() > 0u);

    auto info = resp["result"]["serverInfo"];
    EXPECT_EQ(info["name"].asString(), "zia-server");
}

TEST(LspHandler, InitializedNotification) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    auto resp = handler.handleRequest(makeNotif("initialized"));
    EXPECT_TRUE(resp.empty());
}

TEST(LspHandler, Shutdown) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    auto resp = parseResponse(handler.handleRequest(makeReq("shutdown")));
    EXPECT_EQ(resp["id"].asInt(), 1);
    // result should be null
    EXPECT_TRUE(resp["result"].isNull());
}

TEST(LspHandler, UnknownMethod) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});

    auto resp = parseResponse(handler.handleRequest(makeReq("nonexistent/method")));
    EXPECT_TRUE(resp.has("error"));
}

// ===== Document Sync =====

TEST(LspHandler, DidOpenPublishesDiagnostics) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

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

TEST(LspHandler, DiagnosticsCarryRangeCodeAndRelatedInformation) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

    // "cout" triggers an undefined-identifier error with a did-you-mean note.
    auto params = JsonValue::object({
        {"textDocument",
         JsonValue::object({
             {"uri", JsonValue("file:///test.zia")},
             {"languageId", JsonValue("zia")},
             {"version", JsonValue(1)},
             {"text",
              JsonValue("module Test;\nfunc start() {\n    var count = 1;\n    var x = cout;\n"
                        "    Viper.Terminal.SayInt(x + count);\n}\n")},
         })},
    });
    handler.handleRequest(makeNotif("textDocument/didOpen", std::move(params)));

    EXPECT_TRUE(transport.written.size() > 0u);
    auto notif = JsonValue::parse(transport.written[0]);
    EXPECT_EQ(notif["method"].asString(), "textDocument/publishDiagnostics");

    bool found = false;
    for (const auto &d : notif["params"]["diagnostics"].asArray()) {
        if (!d.has("code") || d["code"].asString() != "V-ZIA-UNDEFINED")
            continue;
        found = true;
        EXPECT_EQ(d["severity"].asInt(), 1); // LSP Error
        // Range derived from the compiler's concrete span: "cout" is 4 chars.
        auto range = d["range"];
        EXPECT_EQ(range["start"]["line"].asInt(), 3); // 0-based line 3
        EXPECT_EQ(range["end"]["line"].asInt(), 3);
        EXPECT_EQ(range["end"]["character"].asInt() - range["start"]["character"].asInt(), 4);
        // Did-you-mean note arrives as relatedInformation in this document.
        EXPECT_TRUE(d.has("relatedInformation"));
        if (d.has("relatedInformation")) {
            EXPECT_TRUE(d["relatedInformation"].size() > 0u);
            auto rel = d["relatedInformation"].at(0);
            EXPECT_EQ(rel["location"]["uri"].asString(), "file:///test.zia");
            EXPECT_TRUE(!rel["message"].asString().empty());
        }
    }
    EXPECT_TRUE(found);
}

TEST(LspHandler, DidChangeUpdatesDiagnostics) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

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
    auto diagnostics = diag["params"]["diagnostics"];
    EXPECT_TRUE(diagnostics.size() > 0u);
    auto range = diagnostics.at(0)["range"];
    EXPECT_TRUE(range["end"]["character"].asInt() > range["start"]["character"].asInt());
}

TEST(LspHandler, DidCloseClearsDiagnostics) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

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

TEST(LspHandler, CompletionAfterDot) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

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
    bool foundDocumentation = false;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto &item = result.at(i);
        if (!item.has("documentation"))
            continue;
        const auto &documentation = item["documentation"];
        if (documentation["kind"].asString() == "markdown" &&
            !documentation["value"].asString().empty()) {
            foundDocumentation = true;
            break;
        }
    }
    EXPECT_TRUE(foundDocumentation);
}

TEST(LspHandler, CompletionOnClosedDoc) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

    // Request completions without opening — should return empty
    auto compParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///nonexistent.zia")}})},
        {"position", JsonValue::object({{"line", JsonValue(0)}, {"character", JsonValue(0)}})},
    });
    auto resp = parseResponse(
        handler.handleRequest(makeReq("textDocument/completion", std::move(compParams))));
    EXPECT_EQ(resp["result"].size(), 0u);
}

TEST(LspHandler, CompletionInvalidParamsReturnsError) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

    auto params = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///x.zia")}})},
    });
    auto resp =
        parseResponse(handler.handleRequest(makeReq("textDocument/completion", std::move(params))));
    EXPECT_EQ(resp["error"]["code"].asInt(), kInvalidParams);
}

// ===== Hover =====

TEST(LspHandler, HoverOnClosedDoc) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

    auto hoverParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///nonexistent.zia")}})},
        {"position", JsonValue::object({{"line", JsonValue(0)}, {"character", JsonValue(0)}})},
    });
    auto resp =
        parseResponse(handler.handleRequest(makeReq("textDocument/hover", std::move(hoverParams))));
    // Should return null result
    EXPECT_TRUE(resp["result"].isNull());
}

TEST(LspHandler, HoverReturnsMarkdownContent) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

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

TEST(LspHandler, HoverOnLocalVariableViaLsp) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

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

TEST(LspHandler, HoverOnWhitespaceReturnsNull) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

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

TEST(LspHandler, DocumentSymbolsListsFunctions) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

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
    for (size_t i = 0; i < result.size(); ++i) {
        if (result.at(i)["name"].asString() == "start") {
            foundStart = true;
            auto range = result.at(i)["location"]["range"];
            EXPECT_TRUE(range["end"]["character"].asInt() > range["start"]["character"].asInt());
        }
    }
    EXPECT_TRUE(foundStart);
}

// ===== Semantic Navigation =====

TEST(LspHandler, DefinitionReferencesAndRenameUseProjectIndex) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

    std::string source = "module Test;\n"
                         "func add(a: Integer, b: Integer) -> Integer { return a + b; }\n"
                         "func start() {\n"
                         "    var value = add(1, 2);\n"
                         "    Viper.Terminal.SayInt(value);\n"
                         "}\n";
    openDocument(handler, "file:///test.zia", source);

    auto defParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
        {"position", positionOf(source, "value);")},
    });
    auto defResp = parseResponse(
        handler.handleRequest(makeReq("textDocument/definition", std::move(defParams))));
    EXPECT_EQ(defResp["result"]["uri"].asString(), "file:///test.zia");
    EXPECT_EQ(defResp["result"]["range"]["start"]["line"].asInt(), 3);

    auto refsParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
        {"position", positionOf(source, "value =")},
        {"context", JsonValue::object({{"includeDeclaration", JsonValue(true)}})},
    });
    auto refsResp = parseResponse(
        handler.handleRequest(makeReq("textDocument/references", std::move(refsParams))));
    EXPECT_TRUE(refsResp["result"].size() >= 2u);

    auto renameParams = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
        {"position", positionOf(source, "value =")},
        {"newName", JsonValue("total")},
    });
    auto renameResp = parseResponse(
        handler.handleRequest(makeReq("textDocument/rename", std::move(renameParams))));
    auto edits = renameResp["result"]["changes"]["file:///test.zia"];
    EXPECT_TRUE(edits.size() >= 2u);
    EXPECT_EQ(edits.at(0)["newText"].asString(), "total");
}

TEST(LspHandler, SignatureHelpReturnsActiveParameter) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

    std::string source = "module Test;\n"
                         "func add(a: Integer, b: Integer) -> Integer { return a + b; }\n"
                         "func start() {\n"
                         "    var value = add(1, 2);\n"
                         "}\n";
    openDocument(handler, "file:///test.zia", source);

    auto params = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
        {"position", positionAfter(source, "add(1, ")},
    });
    auto resp = parseResponse(
        handler.handleRequest(makeReq("textDocument/signatureHelp", std::move(params))));
    EXPECT_TRUE(resp["result"]["signatures"].size() > 0u);
    EXPECT_TRUE(resp["result"]["signatures"].at(0)["label"].asString().find("add") !=
                std::string::npos);
    EXPECT_EQ(resp["result"]["activeParameter"].asInt(), 1);
}

TEST(LspHandler, WorkspaceSymbolSearchesOpenDocuments) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

    std::string source = "module Test;\n"
                         "func add(a: Integer, b: Integer) -> Integer { return a + b; }\n"
                         "func start() { var value = add(1, 2); }\n";
    openDocument(handler, "file:///test.zia", source);

    auto params = JsonValue::object({{"query", JsonValue("add")}});
    auto resp =
        parseResponse(handler.handleRequest(makeReq("workspace/symbol", std::move(params))));
    bool foundAdd = false;
    for (size_t i = 0; i < resp["result"].size(); ++i) {
        if (resp["result"].at(i)["name"].asString() == "add") {
            foundAdd = true;
            EXPECT_EQ(resp["result"].at(i)["location"]["uri"].asString(), "file:///test.zia");
        }
    }
    EXPECT_TRUE(foundAdd);
}

TEST(LspHandler, SemanticTokensFullReturnsDeltaEncodedData) {
    CompilerBridge bridge;
    MockTransport transport;
    LspHandler handler(bridge, transport, {"zia-server", "0.1.0", "zia", "zia", ".zia", "Zia"});
    startLspSession(handler);

    std::string source = "module Test;\nfunc start() {\n    var x = 42;\n}\n";
    openDocument(handler, "file:///test.zia", source);

    auto params = JsonValue::object({
        {"textDocument", JsonValue::object({{"uri", JsonValue("file:///test.zia")}})},
    });
    auto resp = parseResponse(
        handler.handleRequest(makeReq("textDocument/semanticTokens/full", std::move(params))));
    auto data = resp["result"]["data"];
    EXPECT_TRUE(data.size() > 0u);
    EXPECT_EQ(data.size() % 5u, 0u);
}

// ===== DocumentStore =====

TEST(DocumentStore, UriToPathUnix) {
    auto path = DocumentStore::uriToPath("file:///Users/test/file.zia");
    EXPECT_EQ(path, "/Users/test/file.zia");
}

TEST(DocumentStore, UriToPathFileSchemeIsCaseInsensitive) {
    auto path = DocumentStore::uriToPath("FILE:///Users/test/file.zia");
    EXPECT_EQ(path, "/Users/test/file.zia");

    std::string parsed;
    EXPECT_TRUE(DocumentStore::tryFileUriToPath("FiLe:///Users/test/other.zia", parsed));
    EXPECT_EQ(parsed, "/Users/test/other.zia");
}

TEST(DocumentStore, UriToPathWindowsDrive) {
    auto path = DocumentStore::uriToPath("file:///C:/Users/test/file.zia");
    EXPECT_EQ(path, "C:/Users/test/file.zia");
}

TEST(DocumentStore, UriToPathPercentDecode) {
    auto path = DocumentStore::uriToPath("file:///path/my%20file.zia");
    EXPECT_EQ(path, "/path/my file.zia");
}

TEST(DocumentStore, UriToPathPlainPath) {
    auto path = DocumentStore::uriToPath("/just/a/path.zia");
    EXPECT_EQ(path, "/just/a/path.zia");
}

TEST(DocumentStore, UriToPathLocalhostAuthority) {
    auto path = DocumentStore::uriToPath("file://localhost/Users/test/file.zia");
    EXPECT_EQ(path, "/Users/test/file.zia");
}

TEST(DocumentStore, UriToPathUncAuthority) {
    auto path = DocumentStore::uriToPath("file://server/share/file.zia");
    EXPECT_EQ(path, "//server/share/file.zia");
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
