//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/LspHandler.cpp
// Purpose: Implementation of the LSP protocol handler.
// Key invariants:
//   - textDocumentSync is Full (1) — always receives complete document text
//   - Diagnostics published on didOpen and didChange
//   - shutdown sets flag; exit returns appropriate code
//   - Language-specific strings come from ServerConfig
// Ownership/Lifetime:
//   - All returned JSON is fully owned
// Links: tools/lsp-common/LspHandler.hpp, tools/lsp-common/ICompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/LspHandler.hpp"

#include "tools/lsp-common/Transport.hpp"

namespace viper::server {
namespace {

/// @brief Return @p obj's member @p name only if it is itself an Object, else null.
const JsonValue *objectMember(const JsonValue &obj, const char *name) {
    if (obj.type() != JsonType::Object)
        return nullptr;
    const JsonValue *value = obj.get(name);
    return value && value->type() == JsonType::Object ? value : nullptr;
}

/// @brief Return @p obj's member @p name only if it is a String, else null.
const JsonValue *stringMember(const JsonValue &obj, const char *name) {
    if (obj.type() != JsonType::Object)
        return nullptr;
    const JsonValue *value = obj.get(name);
    return value && value->type() == JsonType::String ? value : nullptr;
}

/// @brief Return @p obj's member @p name only if it is an Int, else null.
const JsonValue *intMember(const JsonValue &obj, const char *name) {
    if (obj.type() != JsonType::Object)
        return nullptr;
    const JsonValue *value = obj.get(name);
    return value && value->type() == JsonType::Int ? value : nullptr;
}

/// @brief Extract `params.textDocument.uri` into @p uri.
/// @return true when a non-empty URI string is present.
bool extractTextDocumentUri(const JsonValue &params, std::string &uri) {
    const JsonValue *textDocument = objectMember(params, "textDocument");
    if (!textDocument)
        return false;
    const JsonValue *uriValue = stringMember(*textDocument, "uri");
    if (!uriValue)
        return false;
    uri = uriValue->asString();
    return !uri.empty();
}

/// @brief Extract `params.textDocument.version` into @p version.
/// @return true when an integer version is present.
bool extractTextDocumentVersion(const JsonValue &params, int &version) {
    const JsonValue *textDocument = objectMember(params, "textDocument");
    const JsonValue *versionValue = textDocument ? intMember(*textDocument, "version") : nullptr;
    if (!versionValue)
        return false;
    version = static_cast<int>(versionValue->asInt());
    return true;
}

/// @brief Extract `params.position` into 1-based @p line / @p col.
/// @details LSP positions are 0-based; this adds 1 to each so the values match
///          the frontend's 1-based line/column convention.
/// @return true when both line and character are present and positive.
bool extractPosition(const JsonValue &params, int &line, int &col) {
    const JsonValue *position = objectMember(params, "position");
    const JsonValue *lineValue = position ? intMember(*position, "line") : nullptr;
    const JsonValue *colValue = position ? intMember(*position, "character") : nullptr;
    if (!lineValue || !colValue)
        return false;
    line = static_cast<int>(lineValue->asInt()) + 1;
    col = static_cast<int>(colValue->asInt()) + 1;
    return line > 0 && col > 0;
}

/// @brief Build an LSP `Range` JSON object from 0-based start/end line/character.
JsonValue makeRange(int startLine, int startCharacter, int endLine, int endCharacter) {
    return JsonValue::object({
        {"start",
         JsonValue::object({{"line", JsonValue(startLine)}, {"character", JsonValue(startCharacter)}})},
        {"end",
         JsonValue::object({{"line", JsonValue(endLine)}, {"character", JsonValue(endCharacter)}})},
    });
}

/// @brief Build an LSP `Range` covering the first occurrence of @p name in @p content.
/// @details Scans @p content for @p name, translating the byte offset into a
///          0-based line/character position; falls back to the start of the
///          document (0,0)-(0,1) when @p name is empty or not found. Used to give
///          document symbols a best-effort location without full position info.
JsonValue rangeForFirstName(const std::string &content, const std::string &name) {
    const size_t pos = name.empty() ? std::string::npos : content.find(name);
    if (pos == std::string::npos)
        return makeRange(0, 0, 0, 1);

    int line = 0;
    int character = 0;
    for (size_t i = 0; i < pos; ++i) {
        if (content[i] == '\n') {
            ++line;
            character = 0;
        } else {
            ++character;
        }
    }
    return makeRange(line, character, line, character + static_cast<int>(name.size()));
}

} // namespace

LspHandler::LspHandler(ICompilerBridge &bridge, Transport &transport, const ServerConfig &config)
    : bridge_(bridge), transport_(transport), config_(config) {}

// --- Request dispatch ---

std::string LspHandler::handleRequest(const JsonRpcRequest &req) {
    if (req.method == "initialize")
        return handleInitialize(req);

    if (req.method == "initialized")
        return {}; // Notification

    if (req.method == "shutdown")
        return handleShutdown(req);

    if (req.method == "exit")
        return {}; // Notification — main loop should exit

    // Document sync notifications
    if (req.method == "textDocument/didOpen") {
        handleDidOpen(req);
        return {};
    }
    if (req.method == "textDocument/didChange") {
        handleDidChange(req);
        return {};
    }
    if (req.method == "textDocument/didClose") {
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

std::string LspHandler::handleInitialize(const JsonRpcRequest &req) {
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
         JsonValue::object(
             {{"name", JsonValue(config_.serverName)}, {"version", JsonValue(config_.version)}})},
    });

    return buildResponse(req.id, result);
}

std::string LspHandler::handleShutdown(const JsonRpcRequest &req) {
    shutdownRequested_ = true;
    return buildResponse(req.id, JsonValue());
}

// --- Document sync ---

void LspHandler::handleDidOpen(const JsonRpcRequest &req) {
    const auto *textDoc = objectMember(req.params, "textDocument");
    const auto *uriValue = textDoc ? stringMember(*textDoc, "uri") : nullptr;
    const auto *versionValue = textDoc ? intMember(*textDoc, "version") : nullptr;
    const auto *textValue = textDoc ? stringMember(*textDoc, "text") : nullptr;
    if (!uriValue || !versionValue || !textValue)
        return;
    std::string uri = uriValue->asString();
    int version = static_cast<int>(versionValue->asInt());
    std::string text = textValue->asString();

    store_.open(uri, version, std::move(text));
    publishDiagnostics(uri);
}

void LspHandler::handleDidChange(const JsonRpcRequest &req) {
    std::string uri;
    int version = 0;
    if (!extractTextDocumentUri(req.params, uri) || !extractTextDocumentVersion(req.params, version))
        return;

    // Full sync: take the last content change
    const auto &changes = req.params["contentChanges"];
    if (changes.type() == JsonType::Array && changes.size() > 0) {
        const auto &lastChange = changes.at(changes.size() - 1);
        const auto *textValue = stringMember(lastChange, "text");
        if (!textValue)
            return;
        std::string text = textValue->asString();
        store_.update(uri, version, std::move(text));
    }

    publishDiagnostics(uri);
}

void LspHandler::handleDidClose(const JsonRpcRequest &req) {
    std::string uri;
    if (!extractTextDocumentUri(req.params, uri))
        return;
    store_.close(uri);

    // Clear diagnostics for the closed document
    auto params = JsonValue::object({
        {"uri", JsonValue(uri)},
        {"diagnostics", JsonValue::array({})},
    });
    transport_.writeMessage(buildNotification("textDocument/publishDiagnostics", params));
}

// --- Completion ---

std::string LspHandler::handleCompletion(const JsonRpcRequest &req) {
    std::string uri;
    int line = 0;
    int col = 0;
    if (!extractTextDocumentUri(req.params, uri) || !extractPosition(req.params, line, col))
        return buildError(req.id, kInvalidParams, "Invalid textDocument/completion params");

    const std::string *content = store_.getContent(uri);
    if (!content)
        return buildResponse(req.id, JsonValue::array({}));

    std::string path = DocumentStore::uriToPath(uri);
    auto items = bridge_.completions(*content, line, col, path);

    JsonValue::ArrayType arr;
    arr.reserve(items.size());
    for (const auto &item : items) {
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

std::string LspHandler::handleHover(const JsonRpcRequest &req) {
    std::string uri;
    int line = 0;
    int col = 0;
    if (!extractTextDocumentUri(req.params, uri) || !extractPosition(req.params, line, col))
        return buildError(req.id, kInvalidParams, "Invalid textDocument/hover params");

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

std::string LspHandler::handleDocumentSymbol(const JsonRpcRequest &req) {
    std::string uri;
    if (!extractTextDocumentUri(req.params, uri))
        return buildError(req.id, kInvalidParams, "Invalid textDocument/documentSymbol params");

    const std::string *content = store_.getContent(uri);
    if (!content)
        return buildResponse(req.id, JsonValue::array({}));

    std::string path = DocumentStore::uriToPath(uri);
    auto syms = bridge_.symbols(*content, path);

    JsonValue::ArrayType arr;
    arr.reserve(syms.size());
    for (const auto &s : syms) {
        arr.push_back(JsonValue::object({
            {"name", JsonValue(s.name)},
            {"kind", JsonValue(symbolKindToLsp(s.kind))},
            {"location",
                 JsonValue::object({
                 {"uri", JsonValue(uri)},
                 {"range", rangeForFirstName(*content, s.name)},
             })},
        }));
    }

    return buildResponse(req.id, JsonValue(std::move(arr)));
}

// --- Diagnostic Publishing ---

void LspHandler::publishDiagnostics(const std::string &uri) {
    const std::string *content = store_.getContent(uri);
    if (!content)
        return;

    std::string path = DocumentStore::uriToPath(uri);
    auto diags = bridge_.check(*content, path);

    JsonValue::ArrayType diagArr;
    diagArr.reserve(diags.size());
    for (const auto &d : diags) {
        int lspSeverity;
        switch (d.severity) {
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

        int line = d.line > 0 ? static_cast<int>(d.line) - 1 : 0;
        int col = d.column > 0 ? static_cast<int>(d.column) - 1 : 0;

        auto range = makeRange(line, col, line, col + 1);

        auto diag = JsonValue::object({
            {"range", std::move(range)},
            {"severity", JsonValue(lspSeverity)},
            {"source", JsonValue(config_.sourceName)},
            {"message", JsonValue(d.message)},
        });

        if (!d.code.empty()) {
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

int LspHandler::completionKindToLsp(int kind) {
    switch (kind) {
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

int LspHandler::symbolKindToLsp(const std::string &kind) {
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
        return 5;
    if (kind == "module")
        return 2;
    return 13;
}

} // namespace viper::server
