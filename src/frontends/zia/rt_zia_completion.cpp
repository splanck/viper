//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_zia_completion.cpp
/// @brief extern "C" bridge between the Zia CompletionEngine and the Viper
///        runtime string API (rt_string).
///
/// Lives in fe_zia so it has access to ZiaCompletion.hpp.  The rt_string
/// functions (rt_string_cstr, rt_str_len, rt_string_from_bytes) are declared
/// via rt_string.h but implemented in viper_runtime; symbols resolve at final
/// link time when the executable links both fe_zia and viper_runtime.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/ZiaAnalysis.hpp"
#include "frontends/zia/ZiaCompletion.hpp"
#include "il/io/Serializer.hpp"
#include "runtime/collections/rt_map.h"
#include "runtime/collections/rt_seq.h"
#include "runtime/core/rt_string.h"
#include "runtime/oop/rt_object.h"
#include "support/source_manager.hpp"

#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>

using namespace il::frontends::zia;

namespace {
// s_engine: Only accessed from the compiler thread. No synchronization needed.
// One singleton CompletionEngine per process.  The engine maintains a single-
// entry LRU parse cache keyed by source hash, so repeated calls for the same
// file content do not re-parse.
CompletionEngine s_engine;

std::string toStdString(rt_string value) {
    const char *cstr = value ? rt_string_cstr(value) : "";
    size_t len = value ? (size_t)rt_str_len(value) : 0;
    return std::string(cstr ? cstr : "", len);
}

std::string editorPathOrDefault(rt_string filePath) {
    std::string path = toStdString(filePath);
    return path.empty() ? std::string("<editor>") : path;
}

rt_string toRtString(std::string_view text) {
    const char *data = text.empty() ? "" : text.data();
    return rt_string_from_bytes(data, text.size());
}

void releaseRuntimeObject(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

void mapSetObject(void *map, const char *keyName, void *value) {
    rt_string key = toRtString(keyName);
    rt_map_set(map, key, value);
    rt_string_unref(key);
}

void mapSetInt(void *map, const char *keyName, int64_t value) {
    rt_string key = toRtString(keyName);
    rt_map_set_int(map, key, value);
    rt_string_unref(key);
}

void mapSetBool(void *map, const char *keyName, bool value) {
    rt_string key = toRtString(keyName);
    rt_map_set_bool(map, key, value ? 1 : 0);
    rt_string_unref(key);
}

void mapSetStr(void *map, const char *keyName, std::string_view value) {
    rt_string key = toRtString(keyName);
    rt_string text = toRtString(value);
    rt_map_set_str(map, key, text);
    rt_string_unref(text);
    rt_string_unref(key);
}

int diagnosticSeverityCode(il::support::Severity severity) {
    switch (severity) {
        case il::support::Severity::Warning:
            return 1;
        case il::support::Severity::Note:
            return 2;
        case il::support::Severity::Error:
        default:
            return 0;
    }
}

std::string_view diagnosticSeverityName(il::support::Severity severity) {
    switch (severity) {
        case il::support::Severity::Warning:
            return "warning";
        case il::support::Severity::Note:
            return "note";
        case il::support::Severity::Error:
        default:
            return "error";
    }
}

std::string pathForLocation(const il::support::SourceLoc &loc,
                            const il::support::SourceManager &sm,
                            const std::string &fallbackPath) {
    if (loc.file_id != 0) {
        std::string_view path = sm.getPath(loc.file_id);
        if (!path.empty())
            return std::string(path);
    }
    return fallbackPath;
}

std::string sourcePathForFile(uint32_t fileId,
                              const il::support::SourceManager &sm,
                              const std::string &fallbackPath) {
    if (fileId != 0) {
        std::string_view path = sm.getPath(fileId);
        if (!path.empty())
            return std::string(path);
    }
    return fallbackPath;
}

void *diagnosticToMap(const il::support::Diagnostic &diagnostic,
                      const il::support::SourceManager &sm,
                      const std::string &fallbackPath) {
    il::support::SourceLoc start = diagnostic.loc;
    il::support::SourceLoc end = diagnostic.loc;
    if (diagnostic.range.isValid()) {
        if (!start.isValid())
            start = diagnostic.range.begin;
        end = diagnostic.range.end;
    }

    if (!end.isValid())
        end = start;

    void *map = rt_map_new();
    mapSetStr(map, "file", pathForLocation(start, sm, fallbackPath));
    mapSetInt(map, "line", start.line);
    mapSetInt(map, "column", start.column);
    mapSetInt(map, "endLine", end.line);
    mapSetInt(map, "endColumn", end.column);
    mapSetInt(map, "severity", diagnosticSeverityCode(diagnostic.severity));
    mapSetStr(map, "severityName", diagnosticSeverityName(diagnostic.severity));
    mapSetStr(map, "code", diagnostic.code);
    mapSetStr(map, "message", diagnostic.message);
    mapSetStr(map, "stage", diagnostic.stage);
    mapSetStr(map, "help", diagnostic.help);
    return map;
}

void *diagnosticsToSeq(const il::support::DiagnosticEngine &diagnostics,
                       const il::support::SourceManager &sm,
                       const std::string &fallbackPath) {
    void *seq = rt_seq_new_owned();
    for (const auto &diagnostic : diagnostics.diagnostics()) {
        void *map = diagnosticToMap(diagnostic, sm, fallbackPath);
        rt_seq_push(seq, map);
        releaseRuntimeObject(map);
    }
    return seq;
}
} // namespace

extern "C" {
rt_string rt_zia_complete_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col);
rt_string rt_zia_signature_help_for_file(
    rt_string source, rt_string file_path, int64_t line, int64_t col);
rt_string rt_zia_check_for_file(rt_string source, rt_string file_path);
rt_string rt_zia_hover_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col);
rt_string rt_zia_symbols_for_file(rt_string source, rt_string file_path);
void *rt_zia_toolchain_check_for_file(rt_string source, rt_string file_path);
void *rt_zia_toolchain_compile_for_file(rt_string source, rt_string file_path);

/// @brief Runtime entry point: code completion at (@p line, @p col) in
///        @p source. Returns the serialized completion item list as an
///        rt_string (the editor/LSP bridge consumes this).
rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col) {
    std::string sourceStr = toStdString(source);

    auto items = s_engine.complete(sourceStr, (int)line, (int)col);
    std::string result = serialize(items);

    return rt_string_from_bytes(result.c_str(), result.size());
}

/// @brief As @ref rt_zia_complete but with an explicit @p file_path so
///        cross-file/module-aware completions resolve correctly.
rt_string rt_zia_complete_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    auto items = s_engine.complete(sourceStr, (int)line, (int)col, pathStr);
    std::string result = serialize(items);

    return rt_string_from_bytes(result.c_str(), result.size());
}

/// @brief Runtime entry point: signature help at (@p line, @p col) with no
///        file context (delegates to @ref rt_zia_signature_help_for_file).
rt_string rt_zia_signature_help(rt_string source, int64_t line, int64_t col) {
    return rt_zia_signature_help_for_file(source, nullptr, line, col);
}

/// @brief Runtime entry point: return call signature text for the invocation
///        active at (@p line, @p col), or an empty string if none resolves.
rt_string rt_zia_signature_help_for_file(
    rt_string source, rt_string file_path, int64_t line, int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    std::string result = s_engine.signatureHelp(sourceStr, (int)line, (int)col, pathStr);
    return rt_string_from_bytes(result.c_str(), result.size());
}

/// @brief Runtime entry point: drop the completion engine's cached analysis
///        (call after the project/files change materially).
void rt_zia_completion_clear_cache(void) {
    s_engine.clearCache();
}

// =========================================================================
// Check — run semantic analysis and return serialized diagnostics.
// Format: severity\tline\tcol\tcode\tmessage\n  (one per diagnostic)
// severity: 0=error, 1=warning, 2=note
// =========================================================================
/// @brief Runtime entry point: check @p source with no file context
///        (delegates to @ref rt_zia_check_for_file).
rt_string rt_zia_check(rt_string source) {
    return rt_zia_check_for_file(source, nullptr);
}

/// @brief Runtime entry point: run lex/parse/sema on @p source (named
///        @p file_path) and return serialized diagnostics, one per line as
///        `severity\tline\tcol\tcode\tmessage` (0=error,1=warning,2=note).
rt_string rt_zia_check_for_file(rt_string source, rt_string file_path) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    il::support::SourceManager sm;
    CompilerInput input{.source = sourceStr, .path = pathStr};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result)
        return rt_string_from_bytes("", 0);

    std::ostringstream out;
    for (const auto &d : result->diagnostics.diagnostics()) {
        int sev = 0;
        if (d.severity == il::support::Severity::Warning)
            sev = 1;
        else if (d.severity == il::support::Severity::Note)
            sev = 2;
        out << sev << '\t' << d.loc.line << '\t' << d.loc.column << '\t' << d.code
            << '\t' << d.message << '\n';
    }
    std::string s = out.str();
    return rt_string_from_bytes(s.c_str(), s.size());
}

// =========================================================================
// Structured Toolchain - in-process diagnostics and compile results.
// =========================================================================
/// @brief Runtime entry point: return structured diagnostics for @p source.
/// @details Each diagnostic is a Viper.Collections.Map with file, line,
///          column, endLine, endColumn, severity, severityName, code, message,
///          stage, and help fields.
void *rt_zia_toolchain_check(rt_string source) {
    return rt_zia_toolchain_check_for_file(source, nullptr);
}

/// @brief Runtime entry point: path-aware structured semantic diagnostics.
void *rt_zia_toolchain_check_for_file(rt_string source, rt_string file_path) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    il::support::SourceManager sm;
    CompilerInput input{.source = sourceStr, .path = pathStr};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    const std::string sourcePath =
        result ? sourcePathForFile(result->fileId, sm, pathStr) : pathStr;
    return result ? diagnosticsToSeq(result->diagnostics, sm, sourcePath) : rt_seq_new_owned();
}

/// @brief Runtime entry point: compile @p source to IL and return a structured
///        result map.
void *rt_zia_toolchain_compile(rt_string source) {
    return rt_zia_toolchain_compile_for_file(source, nullptr);
}

/// @brief Runtime entry point: path-aware compile result for IDE tooling.
/// @details Returns a Viper.Collections.Map with success, diagnostics,
///          sourcePath, outputPath, and il fields. `diagnostics` is always a
///          Seq of diagnostic maps, and invalid code returns the diagnostics
///          without requiring string parsing.
void *rt_zia_toolchain_compile_for_file(rt_string source, rt_string file_path) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    il::support::SourceManager sm;
    CompilerInput input{.source = sourceStr, .path = pathStr};
    CompilerOptions opts{};
    CompilerResult result = compile(input, opts, sm);

    const std::string sourcePath = sourcePathForFile(result.fileId, sm, pathStr);
    void *diagnostics = diagnosticsToSeq(result.diagnostics, sm, sourcePath);

    void *map = rt_map_new();
    mapSetBool(map, "success", result.succeeded());
    mapSetStr(map, "sourcePath", sourcePath);
    mapSetStr(map, "outputPath", "");
    mapSetObject(map, "diagnostics", diagnostics);
    releaseRuntimeObject(diagnostics);

    std::string ilText;
    if (result.succeeded())
        ilText = il::io::Serializer::toString(result.module);
    mapSetStr(map, "il", ilText);

    return map;
}

// =========================================================================
// Hover — return type/signature info for the identifier at (line, col).
// Returns empty string if nothing found; otherwise returns a human-readable
// string with the symbol kind and type.
// =========================================================================
/// @brief Runtime entry point: hover info at (@p line, @p col) with no file
///        context (delegates to @ref rt_zia_hover_for_file).
rt_string rt_zia_hover(rt_string source, int64_t line, int64_t col) {
    return rt_zia_hover_for_file(source, nullptr, line, col);
}

/// @brief Runtime entry point: return a human-readable type/signature string
///        for the symbol at (@p line, @p col) in @p source / @p file_path,
///        or the empty string if nothing resolves there.
rt_string rt_zia_hover_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    // Extract identifier at cursor (simple backward scan)
    int lineIdx = (int)line - 1; // 0-based
    int colIdx = (int)col;       // 0-based
    size_t pos = 0;
    int curLine = 0;
    while (curLine < lineIdx && pos < sourceStr.size()) {
        if (sourceStr[pos] == '\n')
            ++curLine;
        ++pos;
    }
    pos += (size_t)colIdx;
    if (pos >= sourceStr.size())
        return rt_string_from_bytes("", 0);

    // Find word boundaries
    size_t start = pos;
    while (start > 0 && (std::isalnum((unsigned char)sourceStr[start - 1]) ||
                          sourceStr[start - 1] == '_'))
        --start;
    size_t end = pos;
    while (end < sourceStr.size() && (std::isalnum((unsigned char)sourceStr[end]) ||
                                       sourceStr[end] == '_'))
        ++end;
    if (start == end)
        return rt_string_from_bytes("", 0);
    std::string ident = sourceStr.substr(start, end - start);

    // Parse and analyze
    il::support::SourceManager sm;
    CompilerInput input{.source = sourceStr, .path = pathStr};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result || !result->sema)
        return rt_string_from_bytes("", 0);

    // Resolve the identifier using the tooling-safe position query so hover
    // respects lexical scope instead of reaching into sema internals.
    const ScopedSymbol *scoped =
        result->sema->findSymbolAtPosition(ident, result->fileId, static_cast<uint32_t>(line),
                                           static_cast<uint32_t>(col + 1));
    if (!scoped)
        return rt_string_from_bytes("", 0);
    const Symbol &sym = scoped->symbol;

    // Format: "kind name: type"
    std::string kindStr;
    switch (sym.kind) {
        case Symbol::Kind::Variable:
            kindStr = "var";
            break;
        case Symbol::Kind::Parameter:
            kindStr = "param";
            break;
        case Symbol::Kind::Function:
            kindStr = "func";
            break;
        case Symbol::Kind::Method:
            kindStr = "method";
            break;
        case Symbol::Kind::Field:
            kindStr = "field";
            break;
        case Symbol::Kind::Type:
            kindStr = "type";
            break;
        case Symbol::Kind::Module:
            kindStr = "module";
            break;
        default:
            kindStr = "symbol";
            break;
    }

    std::string typeStr = sym.type ? sym.type->toDisplayString() : "unknown";
    std::string hover = kindStr + " " + ident + ": " + typeStr;
    if (sym.isFinal)
        hover += " (final)";

    return rt_string_from_bytes(hover.c_str(), hover.size());
}

// =========================================================================
// Symbols — return all top-level symbols in the source.
// Format: name\tkind\ttype\tline\n  (one per symbol)
// =========================================================================
/// @brief Runtime entry point: list symbols in @p source with no file context
///        (delegates to @ref rt_zia_symbols_for_file).
rt_string rt_zia_symbols(rt_string source) {
    return rt_zia_symbols_for_file(source, nullptr);
}

/// @brief Runtime entry point: return all top-level symbols of @p source /
///        @p file_path, serialized one per line as `name\tkind\ttype\tline`.
rt_string rt_zia_symbols_for_file(rt_string source, rt_string file_path) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    il::support::SourceManager sm;
    CompilerInput input{.source = sourceStr, .path = pathStr};
    CompilerOptions opts{};
    uint32_t fileId = sm.addFile(pathStr);
    input.fileId = fileId;

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result || !result->sema)
        return rt_string_from_bytes("", 0);

    std::ostringstream out;
    auto globals = result->sema->getGlobalSymbols();
    for (const auto &sym : globals) {
        std::string kindStr;
        switch (sym.kind) {
            case Symbol::Kind::Variable:
                kindStr = "variable";
                break;
            case Symbol::Kind::Function:
                kindStr = "function";
                break;
            case Symbol::Kind::Type:
                kindStr = "type";
                break;
            case Symbol::Kind::Module:
                kindStr = "module";
                break;
            default:
                kindStr = "symbol";
                break;
        }
        std::string typeStr = sym.type ? sym.type->toDisplayString() : "";
        int symLine = sym.decl ? (int)sym.decl->loc.line : 0;
        out << sym.name << '\t' << kindStr << '\t' << typeStr << '\t' << symLine << '\n';
    }

    // Also include type names (classes, structs, enums)
    auto types = result->sema->getTypeNames();
    for (const auto &tn : types) {
        out << tn << '\t' << "type" << '\t' << tn << '\t' << 0 << '\n';
    }

    std::string s = out.str();
    return rt_string_from_bytes(s.c_str(), s.size());
}

} // extern "C"
