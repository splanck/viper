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
#include "frontends/zia/Lexer.hpp"
#include "frontends/zia/Token.hpp"
#include "frontends/zia/ZiaCompletion.hpp"
#include "il/io/Serializer.hpp"
#include "runtime/collections/rt_map.h"
#include "runtime/collections/rt_seq.h"
#include "runtime/core/rt_string.h"
#include "runtime/oop/rt_object.h"
#include "support/source_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

constexpr int64_t kProjectIndexClassId = INT64_C(-0x460101);

struct IndexedSource {
    std::string path;
    std::string source;
};

struct ProjectIndex {
    explicit ProjectIndex(std::string rootPath) : root(std::move(rootPath)) {}

    std::string root;
    std::unordered_map<std::string, IndexedSource> sources;
};

struct ProjectIndexHandle {
    ProjectIndex *index{nullptr};
};

struct IdentifierToken {
    std::string text;
    il::support::SourceLoc loc{};
    uint32_t endColumn{0};
};

struct SymbolKey {
    bool valid{false};
    std::string semanticName;
    std::string displayName;
    std::string kind;
    std::string typeDisplay;
    std::string ownerType;
    std::string file;
    uint32_t line{0};
    uint32_t column{0};
};

struct SymbolRange {
    bool valid{false};
    std::string file;
    uint32_t line{0};
    uint32_t column{0};
    uint32_t endLine{0};
    uint32_t endColumn{0};
};

std::string normalizeProjectRoot(std::string root) {
    if (root.empty())
        root = ".";
    if (root == "<editor>")
        return root;
    std::error_code ec;
    std::filesystem::path path(root);
    path = std::filesystem::absolute(path, ec);
    if (ec)
        path = std::filesystem::path(root);
    return path.lexically_normal().string();
}

std::string normalizeProjectPath(const ProjectIndex &index, std::string path) {
    if (path.empty())
        return "<editor>";
    if (path == "<editor>")
        return path;

    std::filesystem::path fsPath(path);
    if (fsPath.is_relative() && !index.root.empty() && index.root != "<editor>")
        fsPath = std::filesystem::path(index.root) / fsPath;

    std::error_code ec;
    fsPath = std::filesystem::absolute(fsPath, ec);
    if (ec)
        fsPath = std::filesystem::path(path);
    return fsPath.lexically_normal().string();
}

ProjectIndexHandle *asProjectIndexHandle(void *handle) {
    if (!rt_obj_is_instance(handle, kProjectIndexClassId, sizeof(ProjectIndexHandle)))
        return nullptr;
    return static_cast<ProjectIndexHandle *>(handle);
}

ProjectIndex *asProjectIndex(void *handle) {
    ProjectIndexHandle *typed = asProjectIndexHandle(handle);
    return typed ? typed->index : nullptr;
}

void projectIndexFinalize(void *obj) {
    ProjectIndexHandle *handle = asProjectIndexHandle(obj);
    if (!handle)
        return;
    delete handle->index;
    handle->index = nullptr;
}

std::string symbolKindName(Symbol::Kind kind) {
    switch (kind) {
        case Symbol::Kind::Variable:
            return "variable";
        case Symbol::Kind::Parameter:
            return "parameter";
        case Symbol::Kind::Function:
            return "function";
        case Symbol::Kind::Method:
            return "method";
        case Symbol::Kind::Field:
            return "field";
        case Symbol::Kind::Type:
            return "type";
        case Symbol::Kind::Module:
            return "module";
    }
    return "symbol";
}

std::string baseSymbolName(std::string_view name) {
    size_t pos = name.rfind('.');
    if (pos == std::string_view::npos)
        return std::string(name);
    return std::string(name.substr(pos + 1));
}

std::vector<IdentifierToken> lexIdentifierTokens(const std::string &source, uint32_t fileId) {
    il::support::DiagnosticEngine diagnostics;
    Lexer lexer(source, fileId, diagnostics);

    std::vector<IdentifierToken> result;
    while (true) {
        Token token = lexer.next();
        if (token.kind == TokenKind::Eof)
            break;
        if (token.kind != TokenKind::Identifier)
            continue;
        IdentifierToken ident;
        ident.text = token.text;
        ident.loc = token.loc;
        ident.endColumn = token.loc.column + static_cast<uint32_t>(token.text.size());
        result.push_back(std::move(ident));
    }
    return result;
}

std::optional<IdentifierToken> tokenAtPosition(const std::string &source,
                                               uint32_t fileId,
                                               int64_t line,
                                               int64_t col) {
    if (line <= 0 || col < 0)
        return std::nullopt;
    const uint32_t targetLine = static_cast<uint32_t>(line);
    const uint32_t targetColumn = static_cast<uint32_t>(col + 1);
    for (const auto &token : lexIdentifierTokens(source, fileId)) {
        if (token.loc.line != targetLine)
            continue;
        if (targetColumn < token.loc.column || targetColumn > token.endColumn)
            continue;
        return token;
    }
    return std::nullopt;
}

il::support::SourceLoc symbolDefinitionLoc(const Symbol &symbol,
                                           const il::support::SourceLoc &fallback) {
    if (fallback.isValid())
        return fallback;
    if (symbol.loc.isValid())
        return symbol.loc;
    if (symbol.decl && symbol.decl->loc.isValid())
        return symbol.decl->loc;
    return {};
}

SymbolKey keyForSymbol(const Symbol &symbol,
                       const il::support::SourceLoc &fallbackLoc,
                       std::string_view ownerType,
                       const il::support::SourceManager &sm,
                       const std::string &fallbackPath) {
    il::support::SourceLoc loc = symbolDefinitionLoc(symbol, fallbackLoc);
    if (!loc.isValid())
        return {};

    SymbolKey key;
    key.valid = true;
    key.semanticName = symbol.name;
    key.displayName = baseSymbolName(symbol.name);
    key.kind = symbolKindName(symbol.kind);
    key.typeDisplay = symbol.type ? symbol.type->toDisplayString() : "";
    key.ownerType = std::string(ownerType);
    key.file = pathForLocation(loc, sm, fallbackPath);
    key.line = loc.line;
    key.column = loc.column;
    return key;
}

bool sameSymbolKey(const SymbolKey &lhs, const SymbolKey &rhs) {
    return lhs.valid && rhs.valid && lhs.semanticName == rhs.semanticName &&
           lhs.kind == rhs.kind && lhs.file == rhs.file && lhs.line == rhs.line &&
           lhs.column == rhs.column;
}

std::unique_ptr<AnalysisResult> analyzeIndexedSource(ProjectIndex &index,
                                                     const std::string &path,
                                                     const std::string &source,
                                                     il::support::SourceManager &sm) {
    CompilerInput input{.source = source, .path = path};
    input.sourceProvider = [&index](std::string_view normalizedPath)
        -> std::optional<std::string> {
        auto it = index.sources.find(std::string(normalizedPath));
        if (it == index.sources.end())
            return std::nullopt;
        return it->second.source;
    };
    CompilerOptions opts{};
    return parseAndAnalyze(input, opts, sm);
}

std::optional<SymbolKey> findGlobalSymbolKey(const AnalysisResult &analysis,
                                             const IdentifierToken &token,
                                             const il::support::SourceManager &sm,
                                             const std::string &fallbackPath) {
    std::optional<SymbolKey> importedCandidate;
    for (const auto &symbol : analysis.sema->getGlobalSymbols()) {
        if (symbol.name != token.text && baseSymbolName(symbol.name) != token.text)
            continue;
        SymbolKey key = keyForSymbol(symbol, {}, "", sm, fallbackPath);
        if (!key.valid)
            continue;
        if (symbolDefinitionLoc(symbol, {}).file_id == analysis.fileId)
            return key;
        if (!importedCandidate)
            importedCandidate = std::move(key);
    }
    return importedCandidate;
}

std::optional<SymbolKey> resolveToken(ProjectIndex &index,
                                      const std::string &path,
                                      const std::string &source,
                                      const IdentifierToken &token) {
    il::support::SourceManager sm;
    auto analysis = analyzeIndexedSource(index, path, source, sm);
    if (!analysis || !analysis->sema)
        return std::nullopt;

    const ScopedSymbol *scoped = analysis->sema->findSymbolAtPosition(
        token.text,
        analysis->fileId,
        token.loc.line,
        token.loc.column);
    const std::string sourcePath = sourcePathForFile(analysis->fileId, sm, path);
    if (scoped) {
        SymbolKey key =
            keyForSymbol(scoped->symbol, scoped->loc, scoped->ownerType, sm, sourcePath);
        if (key.valid)
            return key;
    }
    return findGlobalSymbolKey(*analysis, token, sm, sourcePath);
}

std::optional<SymbolKey> resolveAtPosition(ProjectIndex &index,
                                           const std::string &path,
                                           const std::string &source,
                                           int64_t line,
                                           int64_t col) {
    il::support::SourceManager sm;
    auto analysis = analyzeIndexedSource(index, path, source, sm);
    if (!analysis || !analysis->sema)
        return std::nullopt;

    auto token = tokenAtPosition(source, analysis->fileId, line, col);
    if (!token)
        return std::nullopt;

    const ScopedSymbol *scoped = analysis->sema->findSymbolAtPosition(
        token->text,
        analysis->fileId,
        token->loc.line,
        token->loc.column);
    const std::string sourcePath = sourcePathForFile(analysis->fileId, sm, path);
    if (scoped) {
        SymbolKey key =
            keyForSymbol(scoped->symbol, scoped->loc, scoped->ownerType, sm, sourcePath);
        if (key.valid)
            return key;
    }
    return findGlobalSymbolKey(*analysis, *token, sm, sourcePath);
}

SymbolRange definitionRangeForKey(const ProjectIndex &index, const SymbolKey &key) {
    if (!key.valid)
        return {};

    SymbolRange range;
    range.valid = true;
    range.file = key.file;
    range.line = key.line;
    range.column = key.column;
    range.endLine = key.line;
    range.endColumn = key.column + static_cast<uint32_t>(key.displayName.size());

    auto sourceIt = index.sources.find(key.file);
    if (sourceIt == index.sources.end())
        return range;

    il::support::SourceManager sm;
    uint32_t fileId = sm.addFile(key.file);
    sm.setSource(fileId, sourceIt->second.source);
    for (const auto &token : lexIdentifierTokens(sourceIt->second.source, fileId)) {
        if (token.loc.line != key.line)
            continue;
        if (token.text != key.displayName)
            continue;
        if (token.loc.column < key.column)
            continue;
        range.column = token.loc.column;
        range.endColumn = token.endColumn;
        return range;
    }

    for (const auto &token : lexIdentifierTokens(sourceIt->second.source, fileId)) {
        if (token.loc.line != key.line)
            continue;
        if (token.text != key.displayName)
            continue;
        range.column = token.loc.column;
        range.endColumn = token.endColumn;
        return range;
    }
    return range;
}

void *notFoundMap(std::string_view reason) {
    void *map = rt_map_new();
    mapSetBool(map, "found", false);
    mapSetStr(map, "reason", reason);
    return map;
}

void setRangeFields(void *map, const SymbolRange &range) {
    mapSetStr(map, "file", range.file);
    mapSetInt(map, "line", range.line);
    mapSetInt(map, "column", range.column);
    mapSetInt(map, "endLine", range.endLine);
    mapSetInt(map, "endColumn", range.endColumn);
    mapSetInt(map, "editorLine", range.line > 0 ? range.line - 1 : 0);
    mapSetInt(map, "editorColumn", range.column > 0 ? range.column - 1 : 0);
    mapSetInt(map, "editorEndLine", range.endLine > 0 ? range.endLine - 1 : 0);
    mapSetInt(map, "editorEndColumn", range.endColumn > 0 ? range.endColumn - 1 : 0);
}

void *definitionMapForKey(const ProjectIndex &index, const SymbolKey &key) {
    SymbolRange range = definitionRangeForKey(index, key);
    if (!range.valid)
        return notFoundMap("not_found");

    void *map = rt_map_new();
    mapSetBool(map, "found", true);
    setRangeFields(map, range);
    mapSetStr(map, "name", key.displayName);
    mapSetStr(map, "semanticName", key.semanticName);
    mapSetStr(map, "kind", key.kind);
    mapSetStr(map, "type", key.typeDisplay);
    mapSetStr(map, "ownerType", key.ownerType);
    return map;
}

void *referenceMapForToken(const IdentifierToken &token,
                           const std::string &path,
                           const SymbolRange &definitionRange,
                           const SymbolKey &key) {
    void *map = rt_map_new();
    mapSetStr(map, "file", path);
    mapSetInt(map, "line", token.loc.line);
    mapSetInt(map, "column", token.loc.column);
    mapSetInt(map, "endLine", token.loc.line);
    mapSetInt(map, "endColumn", token.endColumn);
    mapSetInt(map, "editorLine", token.loc.line > 0 ? token.loc.line - 1 : 0);
    mapSetInt(map, "editorColumn", token.loc.column > 0 ? token.loc.column - 1 : 0);
    mapSetInt(map, "editorEndLine", token.loc.line > 0 ? token.loc.line - 1 : 0);
    mapSetInt(map, "editorEndColumn", token.endColumn > 0 ? token.endColumn - 1 : 0);
    mapSetStr(map, "name", key.displayName);
    mapSetStr(map, "semanticName", key.semanticName);
    mapSetStr(map, "kind", key.kind);
    const bool isDefinition = definitionRange.valid && path == definitionRange.file &&
                              token.loc.line == definitionRange.line &&
                              token.loc.column == definitionRange.column;
    mapSetBool(map, "isDefinition", isDefinition);
    return map;
}

std::vector<std::string> sortedIndexPaths(const ProjectIndex &index) {
    std::vector<std::string> paths;
    paths.reserve(index.sources.size());
    for (const auto &[path, _] : index.sources)
        paths.push_back(path);
    std::sort(paths.begin(), paths.end());
    return paths;
}

void *referencesForKey(ProjectIndex &index, const SymbolKey &targetKey) {
    void *seq = rt_seq_new_owned();
    if (!targetKey.valid)
        return seq;

    SymbolRange definitionRange = definitionRangeForKey(index, targetKey);
    for (const std::string &path : sortedIndexPaths(index)) {
        auto sourceIt = index.sources.find(path);
        if (sourceIt == index.sources.end())
            continue;

        il::support::SourceManager sm;
        auto analysis = analyzeIndexedSource(index, path, sourceIt->second.source, sm);
        if (!analysis || !analysis->sema)
            continue;

        for (const auto &token : lexIdentifierTokens(sourceIt->second.source, analysis->fileId)) {
            if (token.text != targetKey.displayName)
                continue;

            const ScopedSymbol *scoped = analysis->sema->findSymbolAtPosition(
                token.text, analysis->fileId, token.loc.line, token.loc.column);
            const std::string sourcePath = sourcePathForFile(analysis->fileId, sm, path);
            std::optional<SymbolKey> refKey;
            if (scoped) {
                refKey =
                    keyForSymbol(scoped->symbol, scoped->loc, scoped->ownerType, sm, sourcePath);
            } else {
                refKey = findGlobalSymbolKey(*analysis, token, sm, sourcePath);
            }
            if (!refKey || !sameSymbolKey(*refKey, targetKey))
                continue;

            void *map = referenceMapForToken(token, path, definitionRange, targetKey);
            rt_seq_push(seq, map);
            releaseRuntimeObject(map);
        }
    }
    return seq;
}

bool isIdentifierText(std::string_view text) {
    if (text.empty())
        return false;
    auto isStart = [](char ch) {
        unsigned char c = static_cast<unsigned char>(ch);
        return std::isalpha(c) || ch == '_';
    };
    auto isContinue = [](char ch) {
        unsigned char c = static_cast<unsigned char>(ch);
        return std::isalnum(c) || ch == '_';
    };
    if (!isStart(text.front()))
        return false;
    for (char ch : text.substr(1)) {
        if (!isContinue(ch))
            return false;
    }
    return !Lexer::lookupKeyword(std::string(text)).has_value();
}

bool renameWouldCollide(ProjectIndex &index,
                        const SymbolKey &targetKey,
                        void *references,
                        std::string_view newName) {
    const int64_t count = rt_seq_len(references);
    for (int64_t i = 0; i < count; ++i) {
        void *refMap = rt_seq_get(references, i);
        rt_string fileKey = toRtString("file");
        rt_string lineKey = toRtString("line");
        rt_string columnKey = toRtString("column");
        rt_string fileValue = rt_map_get_str(refMap, fileKey);
        int64_t line = rt_map_get_int(refMap, lineKey);
        int64_t column = rt_map_get_int(refMap, columnKey);
        std::string path = toStdString(fileValue);
        rt_string_unref(fileValue);
        rt_string_unref(columnKey);
        rt_string_unref(lineKey);
        rt_string_unref(fileKey);

        auto sourceIt = index.sources.find(path);
        if (sourceIt == index.sources.end())
            continue;

        IdentifierToken probe;
        probe.text = std::string(newName);
        probe.loc = il::support::SourceLoc{
            0, static_cast<uint32_t>(line), static_cast<uint32_t>(column)};
        probe.endColumn = static_cast<uint32_t>(column + newName.size());

        il::support::SourceManager sm;
        auto analysis = analyzeIndexedSource(index, path, sourceIt->second.source, sm);
        if (!analysis || !analysis->sema)
            continue;
        probe.loc.file_id = analysis->fileId;

        const ScopedSymbol *scoped = analysis->sema->findSymbolAtPosition(
            probe.text, analysis->fileId, probe.loc.line, probe.loc.column);
        const std::string sourcePath = sourcePathForFile(analysis->fileId, sm, path);
        std::optional<SymbolKey> collisionKey;
        if (scoped)
            collisionKey =
                keyForSymbol(scoped->symbol, scoped->loc, scoped->ownerType, sm, sourcePath);
        else
            collisionKey = findGlobalSymbolKey(*analysis, probe, sm, sourcePath);

        if (collisionKey && !sameSymbolKey(*collisionKey, targetKey))
            return true;
    }
    return false;
}

void *renameFailureMap(std::string_view reason) {
    void *map = rt_map_new();
    mapSetBool(map, "success", false);
    mapSetStr(map, "reason", reason);
    void *edits = rt_seq_new_owned();
    mapSetObject(map, "edits", edits);
    releaseRuntimeObject(edits);
    return map;
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
void *rt_zia_project_index_new(rt_string root);
int8_t rt_zia_project_index_is_valid(void *handle);
int8_t rt_zia_project_index_update_file(void *handle, rt_string file_path, rt_string source);
int8_t rt_zia_project_index_remove_file(void *handle, rt_string file_path);
void rt_zia_project_index_clear(void *handle);
void rt_zia_project_index_destroy(void *handle);
void *rt_zia_project_index_definition(
    void *handle, rt_string file_path, rt_string source, int64_t line, int64_t col);
void *rt_zia_project_index_references(
    void *handle, rt_string file_path, rt_string source, int64_t line, int64_t col);
void *rt_zia_project_index_rename_edits(void *handle,
                                        rt_string file_path,
                                        rt_string source,
                                        int64_t line,
                                        int64_t col,
                                        rt_string new_name);

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
// ProjectIndex — project-wide semantic definition/reference/rename queries.
// =========================================================================
void *rt_zia_project_index_new(rt_string root) {
    std::string rootPath = normalizeProjectRoot(toStdString(root));
    auto *handle = static_cast<ProjectIndexHandle *>(
        rt_obj_new_i64(kProjectIndexClassId, sizeof(ProjectIndexHandle)));
    if (!handle)
        return nullptr;
    handle->index = new ProjectIndex(std::move(rootPath));
    rt_obj_set_finalizer(handle, projectIndexFinalize);
    return handle;
}

int8_t rt_zia_project_index_is_valid(void *handle) {
    return asProjectIndex(handle) ? 1 : 0;
}

int8_t rt_zia_project_index_update_file(void *handle, rt_string file_path, rt_string source) {
    ProjectIndex *index = asProjectIndex(handle);
    if (!index)
        return 0;
    std::string normalizedPath = normalizeProjectPath(*index, editorPathOrDefault(file_path));
    index->sources[normalizedPath] = IndexedSource{normalizedPath, toStdString(source)};
    return 1;
}

int8_t rt_zia_project_index_remove_file(void *handle, rt_string file_path) {
    ProjectIndex *index = asProjectIndex(handle);
    if (!index)
        return 0;
    std::string normalizedPath = normalizeProjectPath(*index, editorPathOrDefault(file_path));
    return index->sources.erase(normalizedPath) != 0 ? 1 : 0;
}

void rt_zia_project_index_clear(void *handle) {
    ProjectIndex *index = asProjectIndex(handle);
    if (!index)
        return;
    index->sources.clear();
}

void rt_zia_project_index_destroy(void *handle) {
    ProjectIndexHandle *typed = asProjectIndexHandle(handle);
    if (!typed)
        return;
    delete typed->index;
    typed->index = nullptr;
}

void *rt_zia_project_index_definition(
    void *handle, rt_string file_path, rt_string source, int64_t line, int64_t col) {
    ProjectIndex *index = asProjectIndex(handle);
    if (!index)
        return notFoundMap("invalid_index");

    std::string path = normalizeProjectPath(*index, editorPathOrDefault(file_path));
    std::string sourceStr = toStdString(source);
    index->sources[path] = IndexedSource{path, sourceStr};

    auto key = resolveAtPosition(*index, path, sourceStr, line, col);
    if (!key)
        return notFoundMap("not_found");
    return definitionMapForKey(*index, *key);
}

void *rt_zia_project_index_references(
    void *handle, rt_string file_path, rt_string source, int64_t line, int64_t col) {
    ProjectIndex *index = asProjectIndex(handle);
    if (!index)
        return rt_seq_new_owned();

    std::string path = normalizeProjectPath(*index, editorPathOrDefault(file_path));
    std::string sourceStr = toStdString(source);
    index->sources[path] = IndexedSource{path, sourceStr};

    auto key = resolveAtPosition(*index, path, sourceStr, line, col);
    if (!key)
        return rt_seq_new_owned();
    return referencesForKey(*index, *key);
}

void *rt_zia_project_index_rename_edits(void *handle,
                                        rt_string file_path,
                                        rt_string source,
                                        int64_t line,
                                        int64_t col,
                                        rt_string new_name) {
    ProjectIndex *index = asProjectIndex(handle);
    if (!index)
        return renameFailureMap("invalid_index");

    const std::string newName = toStdString(new_name);
    if (!isIdentifierText(newName))
        return renameFailureMap("invalid_name");

    std::string path = normalizeProjectPath(*index, editorPathOrDefault(file_path));
    std::string sourceStr = toStdString(source);
    index->sources[path] = IndexedSource{path, sourceStr};

    auto key = resolveAtPosition(*index, path, sourceStr, line, col);
    if (!key)
        return renameFailureMap("not_found");

    void *references = referencesForKey(*index, *key);
    if (renameWouldCollide(*index, *key, references, newName)) {
        releaseRuntimeObject(references);
        return renameFailureMap("collision");
    }

    void *edits = rt_seq_new_owned();
    const int64_t count = rt_seq_len(references);
    for (int64_t i = 0; i < count; ++i) {
        void *refMap = rt_seq_get(references, i);
        rt_string fileKey = toRtString("file");
        rt_string lineKey = toRtString("line");
        rt_string columnKey = toRtString("column");
        rt_string endLineKey = toRtString("endLine");
        rt_string endColumnKey = toRtString("endColumn");

        rt_string fileValue = rt_map_get_str(refMap, fileKey);
        const int64_t startLine = rt_map_get_int(refMap, lineKey);
        const int64_t startColumn = rt_map_get_int(refMap, columnKey);
        const int64_t endLine = rt_map_get_int(refMap, endLineKey);
        const int64_t endColumn = rt_map_get_int(refMap, endColumnKey);

        void *edit = rt_map_new();
        mapSetStr(edit, "file", toStdString(fileValue));
        mapSetInt(edit, "startLine", startLine);
        mapSetInt(edit, "startColumn", startColumn);
        mapSetInt(edit, "endLine", endLine);
        mapSetInt(edit, "endColumn", endColumn);
        mapSetInt(edit, "editorStartLine", startLine > 0 ? startLine - 1 : 0);
        mapSetInt(edit, "editorStartColumn", startColumn > 0 ? startColumn - 1 : 0);
        mapSetInt(edit, "editorEndLine", endLine > 0 ? endLine - 1 : 0);
        mapSetInt(edit, "editorEndColumn", endColumn > 0 ? endColumn - 1 : 0);
        mapSetStr(edit, "newText", newName);
        rt_seq_push(edits, edit);
        releaseRuntimeObject(edit);

        rt_string_unref(fileValue);
        rt_string_unref(endColumnKey);
        rt_string_unref(endLineKey);
        rt_string_unref(columnKey);
        rt_string_unref(lineKey);
        rt_string_unref(fileKey);
    }

    void *result = rt_map_new();
    mapSetBool(result, "success", true);
    mapSetStr(result, "reason", "");
    mapSetStr(result, "name", key->displayName);
    mapSetStr(result, "newName", newName);
    mapSetObject(result, "references", references);
    mapSetObject(result, "edits", edits);
    releaseRuntimeObject(edits);
    releaseRuntimeObject(references);
    return result;
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
