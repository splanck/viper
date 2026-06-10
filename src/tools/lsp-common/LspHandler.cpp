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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string_view>

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

/// @brief Convert a JSON integer into an int while enforcing caller-provided bounds.
/// @details JSON-RPC integers are represented as int64_t internally, while the
///          compiler bridges accept int values. This helper prevents oversized
///          protocol values from wrapping during a narrowing cast.
/// @param value JSON value that must be an Int.
/// @param minValue Inclusive lower bound.
/// @param maxValue Inclusive upper bound.
/// @param out Receives the narrowed integer on success.
/// @return True when @p value is an integer inside the requested range.
bool checkedJsonIntToInt(const JsonValue *value, int minValue, int maxValue, int &out) {
    if (!value || value->type() != JsonType::Int)
        return false;
    const int64_t raw = value->asInt();
    if (raw < static_cast<int64_t>(minValue) || raw > static_cast<int64_t>(maxValue))
        return false;
    out = static_cast<int>(raw);
    return true;
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
    return checkedJsonIntToInt(
        versionValue, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), version);
}

/// @brief Extract `params.position` into 1-based @p line / @p col.
/// @details LSP positions are 0-based; this adds 1 to each so the values match
///          the frontend's 1-based line/column convention.
/// @return true when both line and character are present and positive.
bool extractPosition(const JsonValue &params, int &line, int &col) {
    const JsonValue *position = objectMember(params, "position");
    const JsonValue *lineValue = position ? intMember(*position, "line") : nullptr;
    const JsonValue *colValue = position ? intMember(*position, "character") : nullptr;
    int zeroBasedLine = 0;
    int zeroBasedCol = 0;
    if (!checkedJsonIntToInt(lineValue, 0, std::numeric_limits<int>::max() - 1, zeroBasedLine) ||
        !checkedJsonIntToInt(colValue, 0, std::numeric_limits<int>::max() - 1, zeroBasedCol)) {
        return false;
    }
    line = zeroBasedLine + 1;
    col = zeroBasedCol + 1;
    return true;
}

/// @brief Build an LSP `Range` JSON object from 0-based start/end line/character.
JsonValue makeRange(int startLine, int startCharacter, int endLine, int endCharacter) {
    return JsonValue::object({
        {"start",
         JsonValue::object(
             {{"line", JsonValue(startLine)}, {"character", JsonValue(startCharacter)}})},
        {"end",
         JsonValue::object({{"line", JsonValue(endLine)}, {"character", JsonValue(endCharacter)}})},
    });
}

/// @brief Decode the UTF-8 scalar at the front of @p bytes for LSP unit counting.
/// @details Invalid sequences are treated as one replacement character so ranges
///          remain monotonic even when a source buffer contains malformed UTF-8.
/// @param bytes Byte span beginning at the candidate UTF-8 lead byte.
/// @param consumed Receives the number of bytes consumed from @p bytes.
/// @return Number of UTF-16 code units represented by the decoded scalar.
int utf16UnitsForUtf8Lead(std::string_view bytes, size_t &consumed) {
    consumed = 1;
    if (bytes.empty())
        return 0;

    const auto b0 = static_cast<unsigned char>(bytes[0]);
    if (b0 < 0x80)
        return 1;

    auto isContinuation = [](unsigned char c) { return (c & 0xC0u) == 0x80u; };

    uint32_t codePoint = 0;
    size_t expected = 0;
    if ((b0 & 0xE0u) == 0xC0u) {
        codePoint = b0 & 0x1Fu;
        expected = 2;
    } else if ((b0 & 0xF0u) == 0xE0u) {
        codePoint = b0 & 0x0Fu;
        expected = 3;
    } else if ((b0 & 0xF8u) == 0xF0u) {
        codePoint = b0 & 0x07u;
        expected = 4;
    } else {
        return 1;
    }

    if (bytes.size() < expected)
        return 1;
    for (size_t i = 1; i < expected; ++i) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        if (!isContinuation(b))
            return 1;
        codePoint = (codePoint << 6u) | (b & 0x3Fu);
    }

    const bool overlong = (expected == 2 && codePoint < 0x80u) ||
                          (expected == 3 && codePoint < 0x800u) ||
                          (expected == 4 && codePoint < 0x10000u);
    const bool surrogate = codePoint >= 0xD800u && codePoint <= 0xDFFFu;
    if (overlong || surrogate || codePoint > 0x10FFFFu)
        return 1;

    consumed = expected;
    return codePoint > 0xFFFFu ? 2 : 1;
}

/// @brief Count LSP UTF-16 code units in the first @p byteCount bytes of @p text.
/// @details LSP character positions use UTF-16 code units, but compiler source
///          locations and string searches are byte based. The count is clamped
///          to the range of int for direct use in JSON range construction.
/// @param text UTF-8 source line or prefix.
/// @param byteCount Number of bytes from @p text to measure.
/// @return UTF-16 code unit count for the requested prefix.
int utf16CodeUnitsBeforeByte(std::string_view text, size_t byteCount) {
    const size_t limit = std::min(byteCount, text.size());
    int units = 0;
    for (size_t i = 0; i < limit;) {
        size_t consumed = 1;
        const int next = utf16UnitsForUtf8Lead(text.substr(i, limit - i), consumed);
        if (units > std::numeric_limits<int>::max() - next)
            return std::numeric_limits<int>::max();
        units += next;
        i += consumed;
    }
    return units;
}

/// @brief Convert a source byte offset to a zero-based LSP line/character pair.
/// @param content Full UTF-8 source buffer.
/// @param byteOffset Byte offset into @p content; values past EOF are clamped.
/// @param line Receives the zero-based line number.
/// @param character Receives the zero-based UTF-16 character offset.
void byteOffsetToLspPosition(const std::string &content,
                             size_t byteOffset,
                             int &line,
                             int &character) {
    const size_t limit = std::min(byteOffset, content.size());
    size_t lineStart = 0;
    line = 0;
    for (size_t i = 0; i < limit; ++i) {
        if (content[i] == '\n') {
            if (line < std::numeric_limits<int>::max())
                ++line;
            lineStart = i + 1;
        }
    }

    character = utf16CodeUnitsBeforeByte(
        std::string_view(content).substr(lineStart, limit - lineStart), limit - lineStart);
}

/// @brief Return true when @p c is an ASCII identifier byte used by the frontends.
bool isIdentifierByte(char c) {
    const auto uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_';
}

/// @brief Return true when @p pos can begin or end an identifier occurrence.
/// @details This treats the source as a byte string because compiler columns are
///          currently byte-based. Non-ASCII bytes therefore act as boundaries, which
///          is consistent with the current ASCII identifier scanner.
bool isIdentifierBoundary(const std::string &content, size_t pos) {
    return pos == 0 || pos >= content.size() || !isIdentifierByte(content[pos - 1]);
}

/// @brief Find @p name within [begin,end) as a whole identifier token.
/// @details Plain substring search can select occurrences inside comments, strings, or longer
///          identifiers. This helper at least enforces ASCII identifier boundaries so fallback
///          document-symbol ranges do not point into unrelated words.
std::optional<size_t> findIdentifierInRange(const std::string &content,
                                            const std::string &name,
                                            size_t begin,
                                            size_t end) {
    if (name.empty() || begin > end || begin >= content.size())
        return std::nullopt;
    end = std::min(end, content.size());
    size_t pos = content.find(name, begin);
    while (pos != std::string::npos && pos + name.size() <= end) {
        if (isIdentifierBoundary(content, pos) &&
            (pos + name.size() == content.size() || !isIdentifierByte(content[pos + name.size()]))) {
            return pos;
        }
        pos = content.find(name, pos + 1);
    }
    return std::nullopt;
}

/// @brief Build an LSP `Range` from byte offsets in @p content.
/// @details Converts byte positions into zero-based LSP UTF-16 line/character positions.
JsonValue rangeForByteSpan(const std::string &content, size_t start, size_t end) {
    start = std::min(start, content.size());
    end = std::min(std::max(end, start + 1), content.size());
    int startLine = 0;
    int startCharacter = 0;
    int endLine = 0;
    int endCharacter = 0;
    byteOffsetToLspPosition(content, start, startLine, startCharacter);
    byteOffsetToLspPosition(content, end, endLine, endCharacter);
    return makeRange(startLine, startCharacter, endLine, endCharacter);
}

/// @brief Build an LSP `Range` covering a likely declaration occurrence of @p name.
/// @details Used only when semantic source coordinates are unavailable. It prefers
///          whole-token matches and otherwise falls back to a small range at the
///          top of the document so the result is deterministic instead of selecting
///          an arbitrary substring.
JsonValue rangeForFallbackName(const std::string &content, const std::string &name) {
    const auto pos = findIdentifierInRange(content, name, 0, content.size());
    if (!pos)
        return makeRange(0, 0, 0, 1);
    return rangeForByteSpan(content, *pos, *pos + name.size());
}

/// @brief Build an LSP range from a 1-based compiler source location.
/// @details The compiler location points at the declaration token. The helper clamps malformed
/// coordinates to the source line, verifies the expected symbol spelling when possible, and falls
/// back to a same-line whole-token search before using rangeForFallbackName().
JsonValue rangeForSourceLocation(const std::string &content,
                                 const std::string &name,
                                 uint32_t line,
                                 uint32_t column) {
    if (line == 0 || column == 0)
        return rangeForFallbackName(content, name);

    std::size_t lineStart = 0;
    std::size_t currentLine = 1;
    while (currentLine < line && lineStart < content.size()) {
        const std::size_t next = content.find('\n', lineStart);
        if (next == std::string::npos)
            return rangeForFallbackName(content, name);
        lineStart = next + 1;
        ++currentLine;
    }

    std::size_t lineEnd = content.find('\n', lineStart);
    if (lineEnd == std::string::npos)
        lineEnd = content.size();

    std::size_t start = lineStart + static_cast<std::size_t>(column - 1);
    if (start > lineEnd)
        start = lineEnd;

    if (!name.empty() &&
        (start + name.size() > content.size() || content.compare(start, name.size(), name) != 0)) {
        if (const auto sameLine = findIdentifierInRange(content, name, lineStart, lineEnd))
            start = *sameLine;
    }

    const std::size_t end = std::min(content.size(), start + std::max<std::size_t>(name.size(), 1));
    return rangeForByteSpan(content, start, end);
}

/// @brief Find the byte span for an existing zero-based source line.
/// @param content Full source buffer.
/// @param zeroBasedLine Requested source line.
/// @param lineStart Receives the byte offset of the first byte in the line.
/// @param lineEnd Receives the byte offset one past the last byte before '\n'.
/// @return True when @p zeroBasedLine exists in @p content.
bool sourceLineByteSpan(const std::string &content,
                        int zeroBasedLine,
                        size_t &lineStart,
                        size_t &lineEnd) {
    if (zeroBasedLine < 0)
        return false;
    lineStart = 0;
    int line = 0;
    for (size_t i = 0; i < content.size() && line < zeroBasedLine; ++i) {
        if (content[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }
    if (line != zeroBasedLine)
        return false;
    lineEnd = lineStart;
    while (lineEnd < content.size() && content[lineEnd] != '\n')
        ++lineEnd;
    return true;
}

/// @brief Convert a one-based LSP position to one-based compiler byte coordinates.
/// @details LSP `character` values are UTF-16 code units, while the frontends currently consume
///          byte columns. The conversion walks the target source line as UTF-8, clamps past-line
///          positions to the line end, and returns byte columns suitable for hover/completion.
/// @param content Full document text.
/// @param lspOneBasedLine LSP line converted to one-based form.
/// @param lspOneBasedCharacter LSP UTF-16 character converted to one-based form.
/// @param compilerLine Receives the one-based source line.
/// @param compilerColumn Receives the one-based byte column.
/// @return True when the requested line exists in @p content.
bool lspPositionToCompilerPosition(const std::string &content,
                                   int lspOneBasedLine,
                                   int lspOneBasedCharacter,
                                   int &compilerLine,
                                   int &compilerColumn) {
    if (lspOneBasedLine <= 0 || lspOneBasedCharacter <= 0)
        return false;
    size_t lineStart = 0;
    size_t lineEnd = 0;
    const int zeroBasedLine = lspOneBasedLine - 1;
    if (!sourceLineByteSpan(content, zeroBasedLine, lineStart, lineEnd))
        return false;

    const int targetUnits = lspOneBasedCharacter - 1;
    int consumedUnits = 0;
    size_t byteOffset = lineStart;
    while (byteOffset < lineEnd && consumedUnits < targetUnits) {
        size_t consumedBytes = 1;
        const int units = utf16UnitsForUtf8Lead(
            std::string_view(content).substr(byteOffset, lineEnd - byteOffset), consumedBytes);
        if (consumedUnits + units > targetUnits)
            break;
        consumedUnits += units;
        byteOffset += std::min(consumedBytes, lineEnd - byteOffset);
    }

    compilerLine = lspOneBasedLine;
    compilerColumn = static_cast<int>(byteOffset - lineStart) + 1;
    return true;
}

/// @brief Convert a one-based compiler diagnostic location into an LSP range.
/// @details Diagnostic columns are interpreted as byte columns. The generated
///          range is converted to UTF-16 units and clamped to the target line. Identifier tokens
///          are expanded so diagnostics highlight the relevant word instead of a single code unit.
/// @param content Full document text.
/// @param oneBasedLine Compiler diagnostic line number.
/// @param oneBasedColumn Compiler diagnostic column number.
/// @return One-character LSP range for the diagnostic position.
JsonValue diagnosticRangeForLocation(const std::string &content,
                                     uint32_t oneBasedLine,
                                     uint32_t oneBasedColumn) {
    const int line =
        oneBasedLine > 0 && oneBasedLine <= static_cast<uint32_t>(std::numeric_limits<int>::max())
            ? static_cast<int>(oneBasedLine) - 1
            : 0;
    size_t lineStart = 0;
    size_t lineEnd = 0;
    if (!sourceLineByteSpan(content, line, lineStart, lineEnd))
        return makeRange(0, 0, 0, 1);

    const size_t byteColumn = oneBasedColumn > 0 ? static_cast<size_t>(oneBasedColumn - 1u) : 0u;
    const size_t byteOffset = std::min(lineStart + byteColumn, lineEnd);
    int lspLine = 0;
    int lspCol = 0;
    size_t tokenStart = byteOffset;
    size_t tokenEnd = byteOffset;
    if (tokenStart < lineEnd && isIdentifierByte(content[tokenStart])) {
        while (tokenStart > lineStart && isIdentifierByte(content[tokenStart - 1]))
            --tokenStart;
        while (tokenEnd < lineEnd && isIdentifierByte(content[tokenEnd]))
            ++tokenEnd;
    } else {
        tokenEnd = std::min(lineEnd, byteOffset + 1);
    }
    byteOffsetToLspPosition(content, tokenStart, lspLine, lspCol);
    int endLine = 0;
    int endCol = 0;
    byteOffsetToLspPosition(content, tokenEnd, endLine, endCol);
    return makeRange(lspLine, lspCol, endLine, endCol);
}

/// @brief Return true for methods that are request/response-shaped in LSP.
/// @details Notifications must not receive responses. Dispatch uses this helper
///          to suppress accidental responses for missing-id calls to request
///          methods such as initialize, shutdown, completion, hover, and symbols.
bool isRequestMethod(const std::string &method) {
    return method == "initialize" || method == "shutdown" || method == "textDocument/completion" ||
           method == "textDocument/hover" || method == "textDocument/documentSymbol";
}

} // namespace

LspHandler::LspHandler(ICompilerBridge &bridge, Transport &transport, const ServerConfig &config)
    : bridge_(bridge), transport_(transport), config_(config) {}

// --- Request dispatch ---

std::string LspHandler::handleRequest(const JsonRpcRequest &req) {
    if (req.method == "exit")
        return {}; // Main loop handles process exit.

    if (shutdownRequested_) {
        if (req.isNotification())
            return {};
        return buildError(req.id, kInvalidRequest, "Server is shutting down");
    }

    if (req.isNotification() && isRequestMethod(req.method))
        return {};

    if (req.method == "initialize")
        return handleInitialize(req);

    if (req.method == "initialized") {
        if (!initializeResponded_) {
            logMessage(2, "Ignoring initialized notification before initialize");
            return {};
        }
        clientInitialized_ = true;
        return {}; // Notification
    }

    if (req.method == "shutdown")
        return handleShutdown(req);

    if (!clientInitialized_) {
        if (req.isNotification()) {
            logMessage(2, "Ignoring notification before LSP initialization completed: " + req.method);
            return {};
        }
        return buildError(req.id, kInvalidRequest, "Server has not been initialized");
    }

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
    if (initializeResponded_)
        return buildError(req.id, kInvalidRequest, "Server has already been initialized");

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

    initializeResponded_ = true;
    return buildResponse(req.id, result);
}

std::string LspHandler::handleShutdown(const JsonRpcRequest &req) {
    shutdownRequested_ = true;
    return buildResponse(req.id, JsonValue());
}

/// @brief Emit an LSP log-message notification without affecting request flow.
/// @details Notification handlers cannot return JSON-RPC errors, so malformed client notifications
///          are surfaced through the editor log. Transport write failures are handled by the
///          transport layer in the same way as diagnostic notifications.
void LspHandler::logMessage(int type, const std::string &message) {
    auto params = JsonValue::object({
        {"type", JsonValue(static_cast<int64_t>(type))},
        {"message", JsonValue(message)},
    });
    try {
        transport_.writeMessage(buildNotification("window/logMessage", params));
    } catch (const std::exception &) {
        // Logging must not fail the request/notification that triggered it.
    }
}

// --- Document sync ---

void LspHandler::handleDidOpen(const JsonRpcRequest &req) {
    const auto *textDoc = objectMember(req.params, "textDocument");
    const auto *uriValue = textDoc ? stringMember(*textDoc, "uri") : nullptr;
    const auto *versionValue = textDoc ? intMember(*textDoc, "version") : nullptr;
    const auto *textValue = textDoc ? stringMember(*textDoc, "text") : nullptr;
    if (!uriValue || !versionValue || !textValue) {
        logMessage(2, "Ignoring malformed textDocument/didOpen notification");
        return;
    }
    std::string uri = uriValue->asString();
    int version = 0;
    if (!checkedJsonIntToInt(versionValue,
                             std::numeric_limits<int>::min(),
                             std::numeric_limits<int>::max(),
                             version)) {
        logMessage(2, "Ignoring textDocument/didOpen with out-of-range document version: " + uri);
        return;
    }
    std::string text = textValue->asString();

    std::string path;
    std::string pathErr;
    if (!DocumentStore::tryFileUriToPath(uri, path, &pathErr)) {
        logMessage(2, pathErr);
        return;
    }
    (void)path;

    store_.open(uri, version, std::move(text));
    publishDiagnostics(uri);
}

void LspHandler::handleDidChange(const JsonRpcRequest &req) {
    std::string uri;
    int version = 0;
    if (!extractTextDocumentUri(req.params, uri) ||
        !extractTextDocumentVersion(req.params, version)) {
        logMessage(2, "Ignoring malformed textDocument/didChange notification");
        return;
    }
    std::string path;
    std::string pathErr;
    if (!DocumentStore::tryFileUriToPath(uri, path, &pathErr)) {
        logMessage(2, pathErr);
        return;
    }
    (void)path;
    const auto currentVersion = store_.version(uri);
    if (!currentVersion) {
        logMessage(2, "Ignoring textDocument/didChange for unopened document: " + uri);
        return;
    }
    if (version <= *currentVersion) {
        logMessage(4,
                   "Ignoring stale textDocument/didChange for " + uri + " version " +
                       std::to_string(version));
        return;
    }

    // Full sync: every accepted content change must be a complete document body.
    const auto &changes = req.params["contentChanges"];
    if (changes.type() != JsonType::Array || changes.size() == 0) {
        logMessage(2, "Ignoring textDocument/didChange without full document content");
        return;
    }
    const auto &lastChange = changes.at(changes.size() - 1);
    if (lastChange.type() != JsonType::Object || lastChange.has("range") ||
        lastChange.has("rangeLength")) {
        logMessage(2, "Ignoring incremental textDocument/didChange; server only supports full sync");
        return;
    }
    const auto *textValue = stringMember(lastChange, "text");
    if (!textValue) {
        logMessage(2, "Ignoring textDocument/didChange content item without text");
        return;
    }
    std::string text = textValue->asString();
    store_.update(uri, version, std::move(text));

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

    std::string path;
    std::string pathErr;
    if (!DocumentStore::tryFileUriToPath(uri, path, &pathErr))
        return buildError(req.id, kInvalidParams, pathErr);
    int compilerLine = 0;
    int compilerCol = 0;
    if (!lspPositionToCompilerPosition(*content, line, col, compilerLine, compilerCol))
        return buildError(req.id, kInvalidParams, "Invalid textDocument/completion position");
    auto items = bridge_.completions(*content, compilerLine, compilerCol, path);

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

    std::string path;
    std::string pathErr;
    if (!DocumentStore::tryFileUriToPath(uri, path, &pathErr))
        return buildError(req.id, kInvalidParams, pathErr);
    int compilerLine = 0;
    int compilerCol = 0;
    if (!lspPositionToCompilerPosition(*content, line, col, compilerLine, compilerCol))
        return buildError(req.id, kInvalidParams, "Invalid textDocument/hover position");
    auto result = bridge_.hover(*content, compilerLine, compilerCol, path);

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

    std::string path;
    std::string pathErr;
    if (!DocumentStore::tryFileUriToPath(uri, path, &pathErr))
        return buildError(req.id, kInvalidParams, pathErr);
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
                 {"range", rangeForSourceLocation(*content, s.name, s.line, s.column)},
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

    std::string path;
    if (!DocumentStore::tryFileUriToPath(uri, path))
        return;
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

        auto range = diagnosticRangeForLocation(*content, d.line, d.column);

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
