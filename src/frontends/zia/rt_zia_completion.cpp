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
#include "runtime/core/rt_string.h"
#include "support/source_manager.hpp"

#include <cctype>
#include <sstream>
#include <string>

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
} // namespace

extern "C" {
rt_string rt_zia_complete_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col);
rt_string rt_zia_check_for_file(rt_string source, rt_string file_path);
rt_string rt_zia_hover_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col);
rt_string rt_zia_symbols_for_file(rt_string source, rt_string file_path);

rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col) {
    std::string sourceStr = toStdString(source);

    auto items = s_engine.complete(sourceStr, (int)line, (int)col);
    std::string result = serialize(items);

    return rt_string_from_bytes(result.c_str(), result.size());
}

rt_string rt_zia_complete_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    auto items = s_engine.complete(sourceStr, (int)line, (int)col, pathStr);
    std::string result = serialize(items);

    return rt_string_from_bytes(result.c_str(), result.size());
}

void rt_zia_completion_clear_cache(void) {
    s_engine.clearCache();
}

// =========================================================================
// Check — run semantic analysis and return serialized diagnostics.
// Format: severity\tline\tcol\tcode\tmessage\n  (one per diagnostic)
// severity: 0=error, 1=warning, 2=note
// =========================================================================
rt_string rt_zia_check(rt_string source) {
    return rt_zia_check_for_file(source, nullptr);
}

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
// Hover — return type/signature info for the identifier at (line, col).
// Returns empty string if nothing found; otherwise returns a human-readable
// string with the symbol kind and type.
// =========================================================================
rt_string rt_zia_hover(rt_string source, int64_t line, int64_t col) {
    return rt_zia_hover_for_file(source, nullptr, line, col);
}

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
rt_string rt_zia_symbols(rt_string source) {
    return rt_zia_symbols_for_file(source, nullptr);
}

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
