//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/zia-server/LspHandler.cpp
// Purpose: Implementation of the LSP protocol handler.
// Key invariants:
//   - textDocumentSync is Full (1) — always receives complete document text
//   - Diagnostics published on didOpen and didChange
//   - shutdown sets flag; exit returns appropriate code
// Ownership/Lifetime:
//   - All returned JSON is fully owned
// Links: tools/zia-server/LspHandler.hpp, tools/zia-server/CompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/zia-server/LspHandler.hpp"

#include "tools/zia-server/CompilerBridge.hpp"
#include "tools/zia-server/Transport.hpp"

namespace viper::server
{

LspHandler::LspHandler(CompilerBridge &bridge, Transport &transport)
    : bridge_(bridge), transport_(transport)
{
}

// --- Request dispatch ---

std::string LspHandler::handleRequest(const JsonRpcRequest &req)
{
    if (req.method == "initialize")
        return handleInitialize(req);

    if (req.method == "initialized")
        return {}; // Notification

    if (req.method == "shutdown")
        return handleShutdown(req);

    if (req.method == "exit")
        return {}; // Notification — main loop should exit

    // Document sync notifications
    if (req.method == "textDocument/didOpen")
    {
        handleDidOpen(req);
        return {};
    }
    if (req.method == "textDocument/didChange")
    {
        handleDidChange(req);
        return {};
    }
    if (req.method == "textDocument/didClose")
    {
        handleDidClose(req);
        return {};
    }

    // Requests
    if (req.method == "textDocument/completion")
        return handleCompletion(req);

    if (req.method == "textDocument/hover")
        return handleHover(req);

    if (req.method == "textDocument/documentSymbol")
        return handleDocumentSymbol(req);

    if (req.isNotification())
        return {}; // Unknown notification — silently ignore

    return buildError(req.id, kMethodNotFound, "Method not found: " + req.method);
}

// --- Lifecycle ---

std::string LspHandler::handleInitialize(const JsonRpcRequest &req)
{
    auto capabilities = JsonValue::object({
        {"textDocumentSync", JsonValue(1)}, // Full sync
        {"completionProvider",
         JsonValue::object({
             {"triggerCharacters", JsonValue::array({JsonValue(".")})},
         })},
        {"hoverProvider", JsonValue(true)},
        {"documentSymbolProvider", JsonValue(true)},
    });

    auto result = JsonValue::object({
        {"capabilities", std::move(capabilities)},
        {"serverInfo",
         JsonValue::object({{"name", JsonValue("zia-server")}, {"version", JsonValue("0.1.0")}})},
    });

    return buildResponse(req.id, result);
}

std::string LspHandler::handleShutdown(const JsonRpcRequest &req)
{
    shutdownRequested_ = true;
    return buildResponse(req.id, JsonValue());
}

// --- Document sync ---

void LspHandler::handleDidOpen(const JsonRpcRequest &req)
{
    const auto &textDoc = req.params["textDocument"];
    std::string uri = textDoc["uri"].asString();
    int version = static_cast<int>(textDoc["version"].asInt());
    std::string text = textDoc["text"].asString();

    store_.open(uri, version, std::move(text));
    publishDiagnostics(uri);
}

void LspHandler::handleDidChange(const JsonRpcRequest &req)
{
    std::string uri = req.params["textDocument"]["uri"].asString();
    int version = static_cast<int>(req.params["textDocument"]["version"].asInt());

    // Full sync: take the last content change
    const auto &changes = req.params["contentChanges"];
    if (changes.size() > 0)
    {
        std::string text = changes.at(changes.size() - 1)["text"].asString();
        store_.update(uri, version, std::move(text));
    }

    publishDiagnostics(uri);
}

void LspHandler::handleDidClose(const JsonRpcRequest &req)
{
    std::string uri = req.params["textDocument"]["uri"].asString();
    store_.close(uri);

    // Clear diagnostics for the closed document
    auto params = JsonValue::object({
        {"uri", JsonValue(uri)},
        {"diagnostics", JsonValue::array({})},
    });
    transport_.writeMessage(buildNotification("textDocument/publishDiagnostics", params));
}

// --- Completion ---

std::string LspHandler::handleCompletion(const JsonRpcRequest &req)
{
    std::string uri = req.params["textDocument"]["uri"].asString();
    int line = static_cast<int>(req.params["position"]["line"].asInt()) + 1;     // LSP is 0-based
    int col = static_cast<int>(req.params["position"]["character"].asInt()) + 1; // Zia is 1-based

    const std::string *content = store_.getContent(uri);
    if (!content)
        return buildResponse(req.id, JsonValue::array({}));

    std::string path = DocumentStore::uriToPath(uri);
    auto items = bridge_.completions(*content, line, col, path);

    JsonValue::ArrayType arr;
    arr.reserve(items.size());
    for (const auto &item : items)
    {
        arr.push_back(JsonValue::object({
            {"label", JsonValue(item.label)},
            {"insertText", JsonValue(item.insertText)},
            {"kind", JsonValue(completionKindToLsp(item.kind))},
            {"detail", JsonValue(item.detail)},
            {"sortText", JsonValue(std::to_string(item.sortPriority))},
        }));
    }

    return buildResponse(req.id, JsonValue(std::move(arr)));
}

// --- Hover ---

std::string LspHandler::handleHover(const JsonRpcRequest &req)
{
    std::string uri = req.params["textDocument"]["uri"].asString();
    int line = static_cast<int>(req.params["position"]["line"].asInt()) + 1;
    int col = static_cast<int>(req.params["position"]["character"].asInt()) + 1;

    const std::string *content = store_.getContent(uri);
    if (!content)
        return buildResponse(req.id, JsonValue());

    std::string path = DocumentStore::uriToPath(uri);
    auto result = bridge_.hover(*content, line, col, path);

    if (result.empty())
        return buildResponse(req.id, JsonValue());

    auto hover = JsonValue::object({
        {"contents",
         JsonValue::object({
             {"kind", JsonValue("markdown")},
             {"value", JsonValue(result)},
         })},
    });
    return buildResponse(req.id, hover);
}

// --- Document Symbols ---

std::string LspHandler::handleDocumentSymbol(const JsonRpcRequest &req)
{
    std::string uri = req.params["textDocument"]["uri"].asString();

    const std::string *content = store_.getContent(uri);
    if (!content)
        return buildResponse(req.id, JsonValue::array({}));

    std::string path = DocumentStore::uriToPath(uri);
    auto syms = bridge_.symbols(*content, path);

    JsonValue::ArrayType arr;
    arr.reserve(syms.size());
    for (const auto &s : syms)
    {
        // LSP SymbolInformation (simpler than DocumentSymbol)
        arr.push_back(JsonValue::object({
            {"name", JsonValue(s.name)},
            {"kind", JsonValue(symbolKindToLsp(s.kind))},
            {"location",
             JsonValue::object({
                 {"uri", JsonValue(uri)},
                 {"range",
                  JsonValue::object({
                      {"start",
                       JsonValue::object({{"line", JsonValue(0)}, {"character", JsonValue(0)}})},
                      {"end",
                       JsonValue::object({{"line", JsonValue(0)}, {"character", JsonValue(0)}})},
                  })},
             })},
        }));
    }

    return buildResponse(req.id, JsonValue(std::move(arr)));
}

// --- Diagnostic Publishing ---

void LspHandler::publishDiagnostics(const std::string &uri)
{
    const std::string *content = store_.getContent(uri);
    if (!content)
        return;

    std::string path = DocumentStore::uriToPath(uri);
    auto diags = bridge_.check(*content, path);

    JsonValue::ArrayType diagArr;
    diagArr.reserve(diags.size());
    for (const auto &d : diags)
    {
        int lspSeverity;
        switch (d.severity)
        {
            case 0:
                lspSeverity = 3; // LSP Information (Note)
                break;
            case 1:
                lspSeverity = 2; // LSP Warning
                break;
            case 2:
                lspSeverity = 1; // LSP Error
                break;
            default:
                lspSeverity = 3;
                break;
        }

        // LSP positions are 0-based
        int line = d.line > 0 ? static_cast<int>(d.line) - 1 : 0;
        int col = d.column > 0 ? static_cast<int>(d.column) - 1 : 0;

        auto range = JsonValue::object({
            {"start",
             JsonValue::object({{"line", JsonValue(line)}, {"character", JsonValue(col)}})},
            {"end", JsonValue::object({{"line", JsonValue(line)}, {"character", JsonValue(col)}})},
        });

        auto diag = JsonValue::object({
            {"range", std::move(range)},
            {"severity", JsonValue(lspSeverity)},
            {"source", JsonValue("zia")},
            {"message", JsonValue(d.message)},
        });

        if (!d.code.empty())
        {
            auto diagObj = diag.asObject();
            diagObj.push_back({"code", JsonValue(d.code)});
            diag = JsonValue(std::move(diagObj));
        }

        diagArr.push_back(std::move(diag));
    }

    auto params = JsonValue::object({
        {"uri", JsonValue(uri)},
        {"diagnostics", JsonValue(std::move(diagArr))},
    });
    transport_.writeMessage(buildNotification("textDocument/publishDiagnostics", params));
}

// --- Kind mapping ---

int LspHandler::completionKindToLsp(int kind)
{
    // Zia CompletionKind → LSP CompletionItemKind
    // Zia: Keyword=0, Snippet=1, Variable=2, Parameter=3, Field=4,
    //      Method=5, Function=6, Entity=7, Value=8, Interface=9,
    //      Module=10, RuntimeClass=11, Property=12
    // LSP: Text=1, Method=2, Function=3, Field=5, Variable=6, Class=7,
    //      Interface=8, Module=9, Property=10, Value=12, Keyword=14, Snippet=15
    switch (kind)
    {
        case 0:
            return 14; // Keyword
        case 1:
            return 15; // Snippet
        case 2:
            return 6; // Variable
        case 3:
            return 6; // Parameter → Variable
        case 4:
            return 5; // Field
        case 5:
            return 2; // Method
        case 6:
            return 3; // Function
        case 7:
            return 7; // Entity → Class
        case 8:
            return 12; // Value
        case 9:
            return 8; // Interface
        case 10:
            return 9; // Module
        case 11:
            return 7; // RuntimeClass → Class
        case 12:
            return 10; // Property
        default:
            return 1; // Text
    }
}

int LspHandler::symbolKindToLsp(const std::string &kind)
{
    // LSP SymbolKind: File=1, Module=2, Namespace=3, Package=4, Class=5,
    //                 Method=6, Property=7, Field=8, Constructor=9,
    //                 Function=12, Variable=13, String=15
    if (kind == "function")
        return 12;
    if (kind == "method")
        return 6;
    if (kind == "variable")
        return 13;
    if (kind == "parameter")
        return 13;
    if (kind == "field")
        return 8;
    if (kind == "type")
        return 5; // Class
    if (kind == "module")
        return 2;
    return 13; // Default: Variable
}

} // namespace viper::server
