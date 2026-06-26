//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_basic_completion.cpp
/// @brief extern "C" bridge exposing the Viper BASIC IDE engines (diagnostics,
///        completion, …) to the runtime as `Viper.Basic.*`, mirroring the Zia
///        bridge (src/frontends/zia/rt_zia_completion.cpp).
///
/// Lives in fe_basic so it can call parseAndAnalyzeBasic / BasicCompletionEngine
/// without putting editor entry points in the runtime. The rt_string / rt_map /
/// rt_seq symbols are declared here but implemented in viper_runtime; they
/// resolve at final link when the binary links both fe_basic and viper_runtime.
/// Weak stubs in src/runtime/core/rt_basic_completion_stub.c cover binaries that
/// omit fe_basic. Result shapes are identical to the Zia bridge so the IDE
/// controllers consume both. See docs/adr/0014-basic-language-service-runtime-bridge.md.
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicAnalysis.hpp"
#include "frontends/basic/BasicCompletion.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/ast/DeclNodes.hpp"
#include "runtime/collections/rt_map.h"
#include "runtime/collections/rt_seq.h"
#include "runtime/core/rt_string.h"
#include "runtime/oop/rt_object.h"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cctype>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace il::frontends::basic;

namespace {

// One singleton completion engine per process; it keeps a one-entry parse cache
// keyed by source hash, so consecutive keystrokes on one file do not re-parse.
BasicCompletionEngine s_engine;
std::mutex s_engineMutex;

// ── Runtime object/string builders (mirror the Zia bridge helpers) ───────────

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

// ── Diagnostics → Seq<Map> (frontend-agnostic: il::support types) ────────────

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

void *fixitsToSeq(const il::support::Diagnostic &diagnostic) {
    void *seq = rt_seq_new_owned();
    for (const auto &fixit : diagnostic.fixits) {
        void *map = rt_map_new();
        mapSetStr(map, "message", fixit.message);
        mapSetStr(map, "replacement", fixit.replacement);
        mapSetInt(map, "startLine", fixit.range.begin.line);
        mapSetInt(map, "startColumn", fixit.range.begin.column);
        mapSetInt(map, "endLine", fixit.range.end.line);
        mapSetInt(map, "endColumn", fixit.range.end.column);
        rt_seq_push(seq, map);
        releaseRuntimeObject(map);
    }
    return seq;
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
    const bool hasFixit = !diagnostic.fixits.empty();
    mapSetBool(map, "hasFixit", hasFixit);
    void *fixits = fixitsToSeq(diagnostic);
    mapSetObject(map, "fixits", fixits);
    releaseRuntimeObject(fixits);
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

// ── Completion items → Seq<Map> ──────────────────────────────────────────────

std::string_view completionKindName(CompletionKind kind) {
    switch (kind) {
        case CompletionKind::Keyword: return "keyword";
        case CompletionKind::Snippet: return "snippet";
        case CompletionKind::Variable: return "variable";
        case CompletionKind::Parameter: return "parameter";
        case CompletionKind::Field: return "field";
        case CompletionKind::Method: return "method";
        case CompletionKind::Function: return "function";
        case CompletionKind::Entity: return "entity";
        case CompletionKind::Value: return "value";
        case CompletionKind::Interface: return "interface";
        case CompletionKind::Module: return "module";
        case CompletionKind::RuntimeClass: return "runtimeClass";
        case CompletionKind::Property: return "property";
    }
    return "item";
}

void *completionItemToMap(const CompletionItem &item) {
    void *map = rt_map_new();
    mapSetStr(map, "label", item.label);
    mapSetStr(map, "insertText", item.insertText);
    mapSetInt(map, "kind", static_cast<int64_t>(item.kind));
    mapSetStr(map, "kindName", completionKindName(item.kind));
    mapSetStr(map, "detail", item.detail);
    mapSetStr(map, "documentation", "");
    mapSetStr(map, "source", "basic");
    mapSetStr(map, "commitCharacters", "");
    // Omit replacement*/cursorOffset: the IDE defaults them to the cursor
    // position (Map.GetIntOr) so the typed prefix is replaced correctly.
    return map;
}

void *completionItemsToSeq(const std::vector<CompletionItem> &items) {
    void *seq = rt_seq_new_owned();
    for (const auto &item : items) {
        void *map = completionItemToMap(item);
        rt_seq_push(seq, map);
        releaseRuntimeObject(map);
    }
    return seq;
}

// ── Document symbols → tab-delimited string (name\tkind\ttype\tline) ──────────

void emitSymbol(std::ostringstream &out, const std::string &name, const char *kind, uint32_t line) {
    if (!name.empty())
        out << name << '\t' << kind << "\t\t" << line << '\n';
}

std::string basicSymbolsString(const Program &prog) {
    std::ostringstream out;
    for (const auto &p : prog.procs) {
        if (!p)
            continue;
        switch (p->stmtKind()) {
            case Stmt::Kind::FunctionDecl: {
                const auto &fn = static_cast<const FunctionDecl &>(*p);
                emitSymbol(out, fn.name, "function", fn.loc.line);
                break;
            }
            case Stmt::Kind::SubDecl: {
                const auto &sub = static_cast<const SubDecl &>(*p);
                emitSymbol(out, sub.name, "function", sub.loc.line);
                break;
            }
            default:
                break;
        }
    }
    for (const auto &s : prog.main) {
        if (!s)
            continue;
        switch (s->stmtKind()) {
            case Stmt::Kind::ClassDecl: {
                const auto &c = static_cast<const ClassDecl &>(*s);
                emitSymbol(out, c.name, "type", c.loc.line);
                break;
            }
            case Stmt::Kind::TypeDecl: {
                const auto &t = static_cast<const TypeDecl &>(*s);
                emitSymbol(out, t.name, "type", t.loc.line);
                break;
            }
            default:
                break;
        }
    }
    return out.str();
}

// ── Hover: identifier at cursor → type lookup ────────────────────────────────

std::string semaTypeDisplay(SemanticAnalyzer::Type t) {
    switch (t) {
        case SemanticAnalyzer::Type::Int: return "INTEGER";
        case SemanticAnalyzer::Type::Float: return "DOUBLE";
        case SemanticAnalyzer::Type::String: return "STRING";
        case SemanticAnalyzer::Type::Bool: return "BOOLEAN";
        case SemanticAnalyzer::Type::ArrayInt: return "INTEGER()";
        case SemanticAnalyzer::Type::ArrayString: return "STRING()";
        case SemanticAnalyzer::Type::ArrayObject: return "object()";
        case SemanticAnalyzer::Type::Object: return "object";
        default: return "";
    }
}

/// @brief Display name for an AST declared type (DIM/param/return).
std::string astTypeDisplay(Type t) {
    switch (t) {
        case Type::I64: return "INTEGER";
        case Type::F64: return "DOUBLE";
        case Type::Str: return "STRING";
        case Type::Bool: return "BOOLEAN";
    }
    return "";
}

std::string dimTypeDisplay(const DimStmt &d) {
    if (!d.explicitClassQname.empty()) {
        std::string q;
        for (const auto &seg : d.explicitClassQname) {
            if (!q.empty())
                q += ".";
            q += seg;
        }
        return d.isArray ? q + "()" : q;
    }
    std::string base = astTypeDisplay(d.type);
    return (d.isArray && !base.empty()) ? base + "()" : base;
}

std::string dimTypeInStmts(const std::vector<StmtPtr> &stmts, const std::string &canon) {
    for (const auto &s : stmts) {
        if (s && s->stmtKind() == Stmt::Kind::Dim) {
            const auto &d = static_cast<const DimStmt &>(*s);
            if (CanonicalizeIdent(d.name) == canon)
                return dimTypeDisplay(d);
        }
    }
    return "";
}

/// @brief Resolve a hover type for @p ident from the AST when the analyzer's
///        inferred-type table does not track it (e.g. INTEGER, BASIC's default).
///        Covers top-level + proc-local DIMs, parameters, and procedure names.
std::string lookupDeclType(const Program &prog, const std::string &ident) {
    std::string canon = CanonicalizeIdent(ident);
    if (canon.empty())
        return "";
    std::string t = dimTypeInStmts(prog.main, canon);
    if (!t.empty())
        return t;
    for (const auto &p : prog.procs) {
        if (!p)
            continue;
        if (p->stmtKind() == Stmt::Kind::FunctionDecl) {
            const auto &fn = static_cast<const FunctionDecl &>(*p);
            if (CanonicalizeIdent(fn.name) == canon)
                return "FUNCTION -> " + astTypeDisplay(fn.ret);
            for (const auto &prm : fn.params)
                if (CanonicalizeIdent(prm.name) == canon)
                    return astTypeDisplay(prm.type);
            t = dimTypeInStmts(fn.body, canon);
            if (!t.empty())
                return t;
        } else if (p->stmtKind() == Stmt::Kind::SubDecl) {
            const auto &sd = static_cast<const SubDecl &>(*p);
            if (CanonicalizeIdent(sd.name) == canon)
                return "SUB";
            for (const auto &prm : sd.params)
                if (CanonicalizeIdent(prm.name) == canon)
                    return astTypeDisplay(prm.type);
            t = dimTypeInStmts(sd.body, canon);
            if (!t.empty())
                return t;
        }
    }
    return "";
}

bool isBasicIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '%' ||
           c == '!' || c == '#';
}

/// @brief Extract the identifier spanning (1-based @p line, 0-based @p col).
std::string identifierAt(const std::string &source, int line, int col) {
    if (line < 1 || col < 0)
        return "";
    size_t pos = 0, cur = 1;
    while (pos < source.size() && cur < static_cast<size_t>(line)) {
        if (source[pos] == '\n')
            ++cur;
        ++pos;
    }
    if (cur != static_cast<size_t>(line))
        return "";
    size_t lineEnd = source.find('\n', pos);
    if (lineEnd == std::string::npos)
        lineEnd = source.size();
    std::string ln = source.substr(pos, lineEnd - pos);
    size_t at = static_cast<size_t>(col);
    // Cursor sitting just after a word (common) — step back onto it.
    if ((at >= ln.size() || !isBasicIdentChar(ln[at])) && at > 0 && isBasicIdentChar(ln[at - 1]))
        --at;
    if (at >= ln.size() || !isBasicIdentChar(ln[at]))
        return "";
    size_t b = at, e = at;
    while (b > 0 && isBasicIdentChar(ln[b - 1]))
        --b;
    while (e + 1 < ln.size() && isBasicIdentChar(ln[e + 1]))
        ++e;
    return ln.substr(b, e - b + 1);
}

void *basicHoverMap(const std::string &name, const std::string &typeStr) {
    void *map = rt_map_new();
    const bool available = !name.empty() && !typeStr.empty();
    mapSetBool(map, "available", available);
    if (available) {
        mapSetStr(map, "title", name);
        mapSetStr(map, "type", typeStr);
        mapSetStr(map, "display", name + " : " + typeStr);
        mapSetStr(map, "source", "basic");
        mapSetStr(map, "documentation", "");
    }
    return map;
}

} // namespace

extern "C" {

void *rt_basic_toolchain_check_for_file(rt_string source, rt_string file_path) {
    try {
        std::string sourceStr = toStdString(source);
        std::string pathStr = editorPathOrDefault(file_path);
        il::support::SourceManager sm;
        BasicCompilerInput input{.source = sourceStr, .path = pathStr};
        auto result = parseAndAnalyzeBasic(input, sm);
        if (!result)
            return rt_seq_new_owned();
        std::string_view resolved = sm.getPath(result->fileId);
        return diagnosticsToSeq(result->diagnostics, sm,
                                resolved.empty() ? pathStr : std::string(resolved));
    } catch (...) {
        return rt_seq_new_owned();
    }
}

void *rt_basic_completion_items_for_file(rt_string source,
                                         rt_string file_path,
                                         int64_t line,
                                         int64_t col) {
    try {
        std::string sourceStr = toStdString(source);
        std::string pathStr = editorPathOrDefault(file_path);
        std::vector<CompletionItem> items;
        {
            std::lock_guard<std::mutex> lock(s_engineMutex);
            items = s_engine.complete(sourceStr, (int)line, (int)col, pathStr);
        }
        return completionItemsToSeq(items);
    } catch (...) {
        return rt_seq_new_owned();
    }
}

rt_string rt_basic_completion_symbols_for_file(rt_string source, rt_string file_path) {
    try {
        std::string sourceStr = toStdString(source);
        std::string pathStr = editorPathOrDefault(file_path);
        il::support::SourceManager sm;
        BasicCompilerInput input{.source = sourceStr, .path = pathStr};
        auto result = parseAndAnalyzeBasic(input, sm);
        if (!result || !result->ast)
            return rt_string_from_bytes("", 0);
        std::string text = basicSymbolsString(*result->ast);
        return rt_string_from_bytes(text.data(), text.size());
    } catch (...) {
        return rt_string_from_bytes("", 0);
    }
}

void *rt_basic_completion_hover_info_for_file(rt_string source,
                                              rt_string file_path,
                                              int64_t line,
                                              int64_t col) {
    try {
        std::string sourceStr = toStdString(source);
        std::string ident = identifierAt(sourceStr, (int)line, (int)col);
        if (ident.empty())
            return basicHoverMap("", "");
        std::string pathStr = editorPathOrDefault(file_path);
        il::support::SourceManager sm;
        BasicCompilerInput input{.source = sourceStr, .path = pathStr};
        auto result = parseAndAnalyzeBasic(input, sm);
        if (!result || !result->sema)
            return basicHoverMap("", "");
        std::string disp;
        auto t = result->sema->lookupVarType(ident);
        if (t.has_value()) {
            disp = semaTypeDisplay(*t);
            if (*t == SemanticAnalyzer::Type::Object) {
                auto cls = result->sema->lookupObjectClassQName(ident);
                if (cls.has_value() && !cls->empty())
                    disp = *cls;
            }
        }
        // The analyzer only tracks inferred non-default types; fall back to the
        // AST declaration so INTEGER (BASIC's default) and explicit DIM/param/
        // proc types still resolve.
        if (disp.empty())
            disp = lookupDeclType(*result->ast, ident);
        return basicHoverMap(ident, disp);
    } catch (...) {
        return basicHoverMap("", "");
    }
}

} // extern "C"
