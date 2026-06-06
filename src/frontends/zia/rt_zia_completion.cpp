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

#include "frontends/zia/Lexer.hpp"
#include "frontends/zia/Token.hpp"
#include "frontends/zia/ZiaAnalysis.hpp"
#include "frontends/zia/ZiaCompletion.hpp"
#include "il/io/Serializer.hpp"
#include "runtime/collections/rt_map.h"
#include "runtime/collections/rt_seq.h"
#include "runtime/core/rt_string.h"
#include "runtime/oop/rt_object.h"
#include "support/source_manager.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace il::frontends::zia;

namespace {
// s_engine: Only accessed from the compiler thread. No synchronization needed.
// One singleton CompletionEngine per process.  The engine maintains a single-
// entry LRU parse cache keyed by source hash, so repeated calls for the same
// file content do not re-parse.
CompletionEngine s_engine;

constexpr int kMaxSemanticWorkerJobs = 2;
std::atomic<int> g_activeSemanticWorkerJobs{0};

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

struct DiagnosticRecord {
    std::string file;
    int64_t line{0};
    int64_t column{0};
    int64_t endLine{0};
    int64_t endColumn{0};
    int64_t severity{0};
    std::string severityName;
    std::string code;
    std::string message;
    std::string stage;
    std::string help;
    bool hasFixit{false};
    std::string fixitMessage;
    std::string fixitReplacement;
    int64_t fixitStartLine{0};
    int64_t fixitStartColumn{0};
    int64_t fixitEndLine{0};
    int64_t fixitEndColumn{0};
};

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
    if (hasFixit) {
        const auto &fixit = diagnostic.fixits.front();
        mapSetStr(map, "fixitMessage", fixit.message);
        mapSetStr(map, "fixitReplacement", fixit.replacement);
        mapSetInt(map, "fixitStartLine", fixit.range.begin.line);
        mapSetInt(map, "fixitStartColumn", fixit.range.begin.column);
        mapSetInt(map, "fixitEndLine", fixit.range.end.line);
        mapSetInt(map, "fixitEndColumn", fixit.range.end.column);
    } else {
        mapSetStr(map, "fixitMessage", "");
        mapSetStr(map, "fixitReplacement", "");
        mapSetInt(map, "fixitStartLine", 0);
        mapSetInt(map, "fixitStartColumn", 0);
        mapSetInt(map, "fixitEndLine", 0);
        mapSetInt(map, "fixitEndColumn", 0);
    }
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

DiagnosticRecord diagnosticToRecord(const il::support::Diagnostic &diagnostic,
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

    DiagnosticRecord record;
    record.file = pathForLocation(start, sm, fallbackPath);
    record.line = start.line;
    record.column = start.column;
    record.endLine = end.line;
    record.endColumn = end.column;
    record.severity = diagnosticSeverityCode(diagnostic.severity);
    record.severityName = std::string(diagnosticSeverityName(diagnostic.severity));
    record.code = diagnostic.code;
    record.message = diagnostic.message;
    record.stage = diagnostic.stage;
    record.help = diagnostic.help;
    record.hasFixit = !diagnostic.fixits.empty();
    if (record.hasFixit) {
        const auto &fixit = diagnostic.fixits.front();
        record.fixitMessage = fixit.message;
        record.fixitReplacement = fixit.replacement;
        record.fixitStartLine = fixit.range.begin.line;
        record.fixitStartColumn = fixit.range.begin.column;
        record.fixitEndLine = fixit.range.end.line;
        record.fixitEndColumn = fixit.range.end.column;
    }
    return record;
}

void *diagnosticRecordsToSeq(const std::vector<DiagnosticRecord> &diagnostics) {
    void *seq = rt_seq_new_owned();
    for (const auto &diagnostic : diagnostics) {
        void *map = rt_map_new();
        mapSetStr(map, "file", diagnostic.file);
        mapSetInt(map, "line", diagnostic.line);
        mapSetInt(map, "column", diagnostic.column);
        mapSetInt(map, "endLine", diagnostic.endLine);
        mapSetInt(map, "endColumn", diagnostic.endColumn);
        mapSetInt(map, "severity", diagnostic.severity);
        mapSetStr(map, "severityName", diagnostic.severityName);
        mapSetStr(map, "code", diagnostic.code);
        mapSetStr(map, "message", diagnostic.message);
        mapSetStr(map, "stage", diagnostic.stage);
        mapSetStr(map, "help", diagnostic.help);
        mapSetBool(map, "hasFixit", diagnostic.hasFixit);
        mapSetStr(map, "fixitMessage", diagnostic.fixitMessage);
        mapSetStr(map, "fixitReplacement", diagnostic.fixitReplacement);
        mapSetInt(map, "fixitStartLine", diagnostic.fixitStartLine);
        mapSetInt(map, "fixitStartColumn", diagnostic.fixitStartColumn);
        mapSetInt(map, "fixitEndLine", diagnostic.fixitEndLine);
        mapSetInt(map, "fixitEndColumn", diagnostic.fixitEndColumn);
        rt_seq_push(seq, map);
        releaseRuntimeObject(map);
    }
    return seq;
}

constexpr int64_t kProjectIndexClassId = INT64_C(-0x460101);
constexpr int64_t kSemanticJobClassId = INT64_C(-0x460102);

enum class SemanticJobKind : int64_t {
    Unknown = 0,
    CompletionItems = 1,
    SignatureInfo = 2,
    HoverInfo = 3,
    Symbols = 4,
    Diagnostics = 5,
};

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

struct SemanticJob {
    explicit SemanticJob(SemanticJobKind kind) : kind(kind) {}

    SemanticJobKind kind{SemanticJobKind::Unknown};
    std::atomic<bool> done{false};
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::string error;

    std::vector<CompletionItem> completionItems;
    std::string signatureDisplay;
    std::string signatureSource;
    std::string signatureDocumentation;
    int signatureLine{1};
    int signatureCol{0};
    std::string hoverDisplay;
    std::string hoverDocumentation;
    std::string symbols;
    std::vector<DiagnosticRecord> diagnostics;
};

struct SemanticJobHandle {
    std::shared_ptr<SemanticJob> *job{nullptr};
};

bool tryAcquireSemanticWorkerSlot() {
    int active = g_activeSemanticWorkerJobs.load(std::memory_order_acquire);
    while (active < kMaxSemanticWorkerJobs) {
        if (g_activeSemanticWorkerJobs.compare_exchange_weak(
                active, active + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
    }
    return false;
}

void releaseSemanticWorkerSlot() {
    g_activeSemanticWorkerJobs.fetch_sub(1, std::memory_order_acq_rel);
}

void finishSemanticJob(const std::shared_ptr<SemanticJob> &job) {
    job->done.store(true, std::memory_order_release);
    releaseSemanticWorkerSlot();
}

void completeSemanticJobWithError(const std::shared_ptr<SemanticJob> &job, std::string message) {
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        job->error = std::move(message);
    }
    job->done.store(true, std::memory_order_release);
}

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

std::string projectPathLookupKey(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
#ifdef _WIN32
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif
    return path;
}

std::string canonicalProjectPath(const ProjectIndex &index, const std::string &path) {
    std::string normalized = normalizeProjectPath(index, path);
    if (index.sources.find(normalized) != index.sources.end())
        return normalized;

    const std::string lookup = projectPathLookupKey(normalized);
    for (const auto &[indexedPath, _] : index.sources) {
        if (projectPathLookupKey(indexedPath) == lookup)
            return indexedPath;
    }
    return normalized;
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

SemanticJobHandle *asSemanticJobHandle(void *handle) {
    if (!rt_obj_is_instance(handle, kSemanticJobClassId, sizeof(SemanticJobHandle)))
        return nullptr;
    return static_cast<SemanticJobHandle *>(handle);
}

std::shared_ptr<SemanticJob> asSemanticJob(void *handle) {
    SemanticJobHandle *typed = asSemanticJobHandle(handle);
    if (!typed || !typed->job)
        return {};
    return *typed->job;
}

void projectIndexFinalize(void *obj) {
    ProjectIndexHandle *handle = asProjectIndexHandle(obj);
    if (!handle)
        return;
    delete handle->index;
    handle->index = nullptr;
}

void semanticJobFinalize(void *obj) {
    SemanticJobHandle *handle = asSemanticJobHandle(obj);
    if (!handle)
        return;
    if (handle->job) {
        if (*handle->job)
            (*handle->job)->cancelled.store(true, std::memory_order_release);
        delete handle->job;
        handle->job = nullptr;
    }
}

void *semanticJobHandleFor(std::shared_ptr<SemanticJob> job) {
    auto *handle = static_cast<SemanticJobHandle *>(
        rt_obj_new_i64(kSemanticJobClassId, sizeof(SemanticJobHandle)));
    if (!handle)
        return nullptr;
    handle->job = new std::shared_ptr<SemanticJob>(std::move(job));
    rt_obj_set_finalizer(handle, semanticJobFinalize);
    return handle;
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
    return lhs.valid && rhs.valid && lhs.semanticName == rhs.semanticName && lhs.kind == rhs.kind &&
           lhs.file == rhs.file && lhs.line == rhs.line && lhs.column == rhs.column;
}

std::unique_ptr<AnalysisResult> analyzeIndexedSource(ProjectIndex &index,
                                                     const std::string &path,
                                                     const std::string &source,
                                                     il::support::SourceManager &sm) {
    CompilerInput input{.source = source, .path = path};
    input.sourceProvider = [&index](std::string_view normalizedPath) -> std::optional<std::string> {
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
        token.text, analysis->fileId, token.loc.line, token.loc.column);
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
        token->text, analysis->fileId, token->loc.line, token->loc.column);
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
    range.file = canonicalProjectPath(index, key.file);
    range.line = key.line;
    range.column = key.column;
    range.endLine = key.line;
    range.endColumn = key.column + static_cast<uint32_t>(key.displayName.size());

    auto sourceIt = index.sources.find(range.file);
    if (sourceIt == index.sources.end())
        return range;

    il::support::SourceManager sm;
    uint32_t fileId = sm.addFile(range.file);
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
        probe.loc =
            il::support::SourceLoc{0, static_cast<uint32_t>(line), static_cast<uint32_t>(column)};
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

std::string_view completionKindName(CompletionKind kind) {
    switch (kind) {
        case CompletionKind::Keyword:
            return "keyword";
        case CompletionKind::Snippet:
            return "snippet";
        case CompletionKind::Variable:
            return "variable";
        case CompletionKind::Parameter:
            return "parameter";
        case CompletionKind::Field:
            return "field";
        case CompletionKind::Method:
            return "method";
        case CompletionKind::Function:
            return "function";
        case CompletionKind::Entity:
            return "entity";
        case CompletionKind::Value:
            return "value";
        case CompletionKind::Interface:
            return "interface";
        case CompletionKind::Module:
            return "module";
        case CompletionKind::RuntimeClass:
            return "runtimeClass";
        case CompletionKind::Property:
            return "property";
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
    mapSetStr(map, "documentation", item.documentation);
    mapSetStr(map, "source", item.source);
    mapSetStr(map, "commitCharacters", item.commitCharacters);
    mapSetBool(map, "isSnippet", item.isSnippet);
    mapSetInt(map, "cursorOffset", item.cursorOffset);
    mapSetInt(map, "replacementStartLine", item.replacementStartLine);
    mapSetInt(map, "replacementStartColumn", item.replacementStartColumn);
    mapSetInt(map, "replacementEndLine", item.replacementEndLine);
    mapSetInt(map, "replacementEndColumn", item.replacementEndColumn);
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

std::string trimAscii(std::string_view text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return std::string(text.substr(start, end - start));
}

bool startsWithAscii(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string readIdentifier(std::string_view text, size_t start) {
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    size_t end = start;
    while (end < text.size() &&
           (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
        ++end;
    return std::string(text.substr(start, end - start));
}

bool declarationLineMatchesName(std::string_view line, std::string_view name) {
    std::string text = trimAscii(line);
    if (startsWithAscii(text, "expose "))
        text = trimAscii(std::string_view(text).substr(7));
    if (startsWithAscii(text, "final "))
        text = trimAscii(std::string_view(text).substr(6));

    constexpr std::string_view keywords[] = {
        "func ",
        "class ",
        "struct ",
        "interface ",
        "var ",
    };
    for (std::string_view keyword : keywords) {
        if (startsWithAscii(text, keyword))
            return readIdentifier(text, keyword.size()) == std::string(name);
    }
    return false;
}

std::string docCommentBeforeLine(const std::vector<std::string> &lines, size_t declLine) {
    if (declLine == 0 || declLine > lines.size())
        return {};

    std::vector<std::string> docs;
    size_t i = declLine;
    while (i > 0) {
        --i;
        std::string text = trimAscii(lines[i]);
        if (startsWithAscii(text, "///"))
            docs.push_back(trimAscii(std::string_view(text).substr(3)));
        else
            break;
    }

    std::string doc;
    for (auto it = docs.rbegin(); it != docs.rend(); ++it) {
        if (!doc.empty())
            doc += "\n";
        doc += *it;
    }
    return doc;
}

std::string docCommentBeforeLoc(const il::support::SourceManager &sm, il::support::SourceLoc loc) {
    if (!loc.isValid() || loc.line <= 1)
        return {};

    std::vector<std::string> docs;
    uint32_t line = loc.line - 1;
    while (line > 0) {
        std::string text = trimAscii(sm.getLine(loc.file_id, line));
        if (startsWithAscii(text, "///")) {
            docs.push_back(trimAscii(std::string_view(text).substr(3)));
        } else {
            break;
        }
        --line;
    }

    std::string doc;
    for (auto it = docs.rbegin(); it != docs.rend(); ++it) {
        if (!doc.empty())
            doc += "\n";
        doc += *it;
    }
    return doc;
}

std::string documentationForSymbolLoc(const il::support::SourceManager &sm, const Symbol &sym) {
    if (!sym.documentation.empty())
        return sym.documentation;
    il::support::SourceLoc loc = sym.loc.isValid() ? sym.loc : il::support::SourceLoc{};
    if (!loc.isValid() && sym.decl)
        loc = sym.decl->loc;
    return docCommentBeforeLoc(sm, loc);
}

std::string declarationDocumentationForName(std::string_view source, std::string_view name) {
    if (source.empty() || name.empty())
        return {};

    std::vector<std::string> lines;
    std::istringstream input{std::string(source)};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        if (declarationLineMatchesName(lines[i], name))
            return docCommentBeforeLine(lines, i);
    }
    return {};
}

std::string signatureNameFromDisplay(std::string_view display) {
    std::string firstLine(display.substr(0, display.find('\n')));
    size_t open = firstLine.find('(');
    if (open == std::string::npos)
        return {};
    return trimAscii(std::string_view(firstLine).substr(0, open));
}

std::string firstDisplayLine(std::string_view display) {
    return std::string(display.substr(0, display.find('\n')));
}

std::string documentationFromDisplayTail(std::string_view display) {
    size_t pos = display.find('\n');
    if (pos == std::string_view::npos)
        return {};
    ++pos;

    std::string doc;
    while (pos <= display.size()) {
        size_t next = display.find('\n', pos);
        std::string_view line =
            next == std::string_view::npos ? display.substr(pos) : display.substr(pos, next - pos);
        std::string trimmed = trimAscii(line);
        if (!trimmed.empty() && trimmed.rfind("parameter ", 0) != 0) {
            if (!doc.empty())
                doc += "\n";
            doc += trimmed;
        }
        if (next == std::string_view::npos)
            break;
        pos = next + 1;
    }
    return doc;
}

bool looksLikeSignatureLine(std::string_view line) {
    std::string trimmed = trimAscii(line);
    if (trimmed.empty() || trimmed.rfind("parameter ", 0) == 0)
        return false;
    size_t open = trimmed.find('(');
    if (open == std::string::npos)
        return false;
    size_t colon = trimmed.find(':');
    return colon == std::string::npos || colon > open;
}

std::vector<std::string> splitSignatureDisplays(std::string_view display) {
    std::vector<std::string> blocks;
    std::string current;
    size_t pos = 0;
    while (pos <= display.size()) {
        size_t next = display.find('\n', pos);
        std::string_view line =
            next == std::string_view::npos ? display.substr(pos) : display.substr(pos, next - pos);
        if (looksLikeSignatureLine(line) && !current.empty()) {
            blocks.push_back(std::move(current));
            current.clear();
        }
        if (!line.empty()) {
            if (!current.empty())
                current += "\n";
            current.append(line);
        }
        if (next == std::string_view::npos)
            break;
        pos = next + 1;
    }
    if (!current.empty())
        blocks.push_back(std::move(current));
    return blocks;
}

void *signatureParametersToSeq(std::string_view signatureLine);

void *signatureOverloadsToSeq(const std::vector<std::string> &overloads) {
    void *seq = rt_seq_new_owned();
    for (const auto &overload : overloads) {
        std::string firstLine = firstDisplayLine(overload);
        void *map = rt_map_new();
        mapSetStr(map, "display", firstLine);
        mapSetStr(map, "name", signatureNameFromDisplay(firstLine));
        size_t arrow = firstLine.find("->");
        std::string returnType = arrow == std::string::npos
                                     ? std::string()
                                     : trimAscii(std::string_view(firstLine).substr(arrow + 2));
        mapSetStr(map, "returnType", returnType);
        mapSetStr(map, "documentation", documentationFromDisplayTail(overload));
        void *params = signatureParametersToSeq(firstLine);
        mapSetObject(map, "parameters", params);
        releaseRuntimeObject(params);
        rt_seq_push(seq, map);
        releaseRuntimeObject(map);
    }
    return seq;
}

std::string hoverNameFromDisplay(std::string_view display) {
    std::string text = firstDisplayLine(display);
    size_t firstSpace = text.find(' ');
    size_t colon = text.find(':');
    if (firstSpace == std::string::npos || colon == std::string::npos || colon <= firstSpace)
        return {};
    return trimAscii(std::string_view(text).substr(firstSpace + 1, colon - firstSpace - 1));
}

bool isIdentByte(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string qualifierBeforeIdentifier(std::string_view source, size_t identStart) {
    if (identStart == 0 || source[identStart - 1] != '.')
        return {};

    size_t end = identStart - 1;
    size_t start = end;
    while (start > 0 && isIdentByte(source[start - 1]))
        --start;
    if (start == end)
        return {};
    return std::string(source.substr(start, end - start));
}

bool symbolNameMatchesIdentifier(const Symbol &sym, std::string_view ident) {
    if (sym.name == ident)
        return true;
    size_t dot = sym.name.rfind('.');
    return dot != std::string::npos && std::string_view(sym.name).substr(dot + 1) == ident;
}

std::optional<Symbol> moduleExportSymbolAt(const Sema &sema,
                                           std::string_view source,
                                           size_t identStart,
                                           std::string_view ident) {
    std::string moduleName = qualifierBeforeIdentifier(source, identStart);
    if (moduleName.empty())
        return std::nullopt;

    for (const auto &sym : sema.getModuleExports(moduleName)) {
        if (symbolNameMatchesIdentifier(sym, ident))
            return sym;
    }
    return std::nullopt;
}

std::vector<std::string> splitDottedIdentifier(std::string_view text) {
    std::vector<std::string> parts;
    std::string part;
    for (char c : text) {
        if (c == '.') {
            if (!part.empty())
                parts.push_back(part);
            part.clear();
        } else {
            part += c;
        }
    }
    if (!part.empty())
        parts.push_back(part);
    return parts;
}

std::optional<Symbol> runtimeMemberSymbolAt(const Sema &sema,
                                            std::string_view source,
                                            size_t identStart,
                                            std::string_view ident) {
    std::string qualifier = qualifierBeforeIdentifier(source, identStart);
    if (qualifier.empty())
        return std::nullopt;

    auto parts = splitDottedIdentifier(qualifier);
    if (parts.empty())
        return std::nullopt;

    std::string className = sema.resolveModuleAlias(parts[0]);
    if (!className.empty()) {
        for (size_t i = 1; i < parts.size(); ++i)
            className += "." + parts[i];
    } else {
        className = qualifier;
    }

    for (const auto &sym : sema.getRuntimeMembers(className)) {
        if (symbolNameMatchesIdentifier(sym, ident))
            return sym;
    }
    return std::nullopt;
}

std::string hoverKindForSymbol(const Symbol &sym) {
    switch (sym.kind) {
        case Symbol::Kind::Variable:
            return "var";
        case Symbol::Kind::Parameter:
            return "param";
        case Symbol::Kind::Function:
            return "func";
        case Symbol::Kind::Method:
            return "method";
        case Symbol::Kind::Field:
            return "field";
        case Symbol::Kind::Type:
            return "type";
        case Symbol::Kind::Module:
            return "module";
        default:
            return "symbol";
    }
}

std::string formatHoverForSymbol(const il::support::SourceManager &sm,
                                 const Symbol &sym,
                                 std::string_view displayName) {
    std::string typeStr = sym.type ? sym.type->toDisplayString() : "unknown";
    std::string hover = hoverKindForSymbol(sym) + " " + std::string(displayName) + ": " + typeStr;
    if (sym.isFinal)
        hover += " (final)";
    std::string doc = documentationForSymbolLoc(sm, sym);
    if (!doc.empty())
        hover += "\n" + doc;
    return hover;
}

int activeParameterForSource(std::string_view source, int line, int col) {
    if (source.empty())
        return 0;
    if (line < 1)
        line = 1;
    if (col < 0)
        col = 0;

    size_t cursor = 0;
    int curLine = 1;
    while (cursor < source.size() && curLine < line) {
        if (source[cursor] == '\n')
            ++curLine;
        ++cursor;
    }
    size_t lineStart = cursor;
    while (cursor < source.size() && source[cursor] != '\n')
        ++cursor;
    size_t at = lineStart + (col < 0 ? 0 : static_cast<size_t>(col));
    if (at > cursor)
        at = cursor;

    int depth = 0;
    size_t open = std::string_view::npos;
    for (size_t i = at; i > 0; --i) {
        char c = source[i - 1];
        if (c == ')')
            ++depth;
        else if (c == '(') {
            if (depth == 0) {
                open = i - 1;
                break;
            }
            --depth;
        }
    }
    if (open == std::string_view::npos)
        return 0;

    int param = 0;
    int nestedParen = 0;
    int nestedBracket = 0;
    int nestedBrace = 0;
    for (size_t i = open + 1; i < at && i < source.size(); ++i) {
        char c = source[i];
        if (c == '(')
            ++nestedParen;
        else if (c == ')' && nestedParen > 0)
            --nestedParen;
        else if (c == '[')
            ++nestedBracket;
        else if (c == ']' && nestedBracket > 0)
            --nestedBracket;
        else if (c == '{')
            ++nestedBrace;
        else if (c == '}' && nestedBrace > 0)
            --nestedBrace;
        else if (c == ',' && nestedParen == 0 && nestedBracket == 0 && nestedBrace == 0)
            ++param;
    }
    return param;
}

std::vector<std::string> splitTopLevelParams(std::string_view params) {
    std::vector<std::string> out;
    int nestedParen = 0;
    int nestedBracket = 0;
    int nestedBrace = 0;
    size_t start = 0;
    for (size_t i = 0; i < params.size(); ++i) {
        char c = params[i];
        if (c == '(')
            ++nestedParen;
        else if (c == ')' && nestedParen > 0)
            --nestedParen;
        else if (c == '[')
            ++nestedBracket;
        else if (c == ']' && nestedBracket > 0)
            --nestedBracket;
        else if (c == '{')
            ++nestedBrace;
        else if (c == '}' && nestedBrace > 0)
            --nestedBrace;
        else if (c == ',' && nestedParen == 0 && nestedBracket == 0 && nestedBrace == 0) {
            out.push_back(trimAscii(params.substr(start, i - start)));
            start = i + 1;
        }
    }
    std::string tail = trimAscii(params.substr(start));
    if (!tail.empty())
        out.push_back(std::move(tail));
    return out;
}

void *signatureParametersToSeq(std::string_view signatureLine) {
    void *seq = rt_seq_new_owned();
    size_t open = signatureLine.find('(');
    size_t close = signatureLine.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
        return seq;

    auto params = splitTopLevelParams(signatureLine.substr(open + 1, close - open - 1));
    for (const auto &paramText : params) {
        if (paramText.empty())
            continue;
        void *param = rt_map_new();
        size_t colon = paramText.find(':');
        if (colon == std::string::npos) {
            mapSetStr(param, "name", paramText);
            mapSetStr(param, "type", "");
        } else {
            mapSetStr(param, "name", trimAscii(std::string_view(paramText).substr(0, colon)));
            mapSetStr(param, "type", trimAscii(std::string_view(paramText).substr(colon + 1)));
        }
        mapSetStr(param, "documentation", "");
        rt_seq_push(seq, param);
        releaseRuntimeObject(param);
    }
    return seq;
}

void *signatureInfoMap(std::string_view display,
                       std::string_view source,
                       int line,
                       int col,
                       std::string_view documentation,
                       bool computeDocumentation) {
    void *map = rt_map_new();
    bool available = !display.empty();
    mapSetBool(map, "available", available);
    std::vector<std::string> overloads =
        available ? splitSignatureDisplays(display) : std::vector<std::string>{};
    if (available && overloads.empty())
        overloads.push_back(std::string(display));
    std::string activeDisplay = overloads.empty() ? std::string() : overloads.front();
    std::string firstLine = firstDisplayLine(activeDisplay);
    mapSetStr(map, "display", firstLine);
    mapSetInt(map, "activeParameter", available ? activeParameterForSource(source, line, col) : 0);
    mapSetInt(map, "activeSignature", 0);
    mapSetInt(map, "overloadCount", static_cast<int64_t>(overloads.size()));
    std::string name = signatureNameFromDisplay(firstLine);
    mapSetStr(map, "name", name);
    size_t arrow = firstLine.find("->");
    std::string returnType = arrow == std::string::npos
                                 ? std::string()
                                 : trimAscii(std::string_view(firstLine).substr(arrow + 2));
    mapSetStr(map, "returnType", returnType);
    void *params = signatureParametersToSeq(firstLine);
    mapSetObject(map, "parameters", params);
    releaseRuntimeObject(params);
    void *overloadSeq = signatureOverloadsToSeq(overloads);
    mapSetObject(map, "overloads", overloadSeq);
    releaseRuntimeObject(overloadSeq);
    std::string doc(documentation);
    if (doc.empty())
        doc = documentationFromDisplayTail(activeDisplay);
    if (computeDocumentation && doc.empty())
        doc = declarationDocumentationForName(source, name);
    mapSetStr(map, "documentation", doc);
    mapSetStr(map, "source", "zia");
    return map;
}

void *hoverInfoMap(std::string_view display,
                   std::string_view source,
                   std::string_view documentation,
                   bool computeDocumentation) {
    void *map = rt_map_new();
    bool available = !display.empty();
    mapSetBool(map, "available", available);
    std::string firstLine = firstDisplayLine(display);
    mapSetStr(map, "display", firstLine);

    std::string text = firstLine;
    std::string kind;
    std::string name;
    std::string type;
    size_t firstSpace = text.find(' ');
    size_t colon = text.find(':');
    if (firstSpace != std::string::npos)
        kind = trimAscii(std::string_view(text).substr(0, firstSpace));
    if (firstSpace != std::string::npos && colon != std::string::npos && colon > firstSpace)
        name = trimAscii(std::string_view(text).substr(firstSpace + 1, colon - firstSpace - 1));
    if (colon != std::string::npos)
        type = trimAscii(std::string_view(text).substr(colon + 1));
    mapSetStr(map, "kind", kind);
    mapSetStr(map, "name", name);
    mapSetStr(map, "type", type);
    mapSetStr(map, "title", name.empty() ? text : name);
    std::string doc(documentation);
    if (doc.empty())
        doc = documentationFromDisplayTail(display);
    if (computeDocumentation && doc.empty())
        doc = declarationDocumentationForName(source, name);
    mapSetStr(map, "documentation", doc);
    mapSetStr(map, "source", "zia");
    return map;
}

std::vector<DiagnosticRecord> diagnosticRecordsForSource(const std::string &source,
                                                         const std::string &path) {
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result)
        return {};

    const std::string sourcePath = sourcePathForFile(result->fileId, sm, path);
    std::vector<DiagnosticRecord> records;
    records.reserve(result->diagnostics.diagnostics().size());
    for (const auto &diagnostic : result->diagnostics.diagnostics())
        records.push_back(diagnosticToRecord(diagnostic, sm, sourcePath));
    return records;
}

std::string hoverForSource(const std::string &source,
                           const std::string &path,
                           int64_t line,
                           int64_t col) {
    int lineIdx = (int)line - 1;
    int colIdx = (int)col;
    size_t pos = 0;
    int curLine = 0;
    while (curLine < lineIdx && pos < source.size()) {
        if (source[pos] == '\n')
            ++curLine;
        ++pos;
    }
    pos += (size_t)colIdx;
    if (pos >= source.size())
        return {};

    size_t start = pos;
    while (start > 0 && isIdentByte(source[start - 1]))
        --start;
    size_t end = pos;
    while (end < source.size() && isIdentByte(source[end]))
        ++end;
    if (start == end)
        return {};
    std::string ident = source.substr(start, end - start);

    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result || !result->sema)
        return {};

    const ScopedSymbol *scoped = result->sema->findSymbolAtPosition(
        ident, result->fileId, static_cast<uint32_t>(line), static_cast<uint32_t>(col + 1));
    if (scoped)
        return formatHoverForSymbol(sm, scoped->symbol, ident);

    if (auto exportSym = moduleExportSymbolAt(*result->sema, source, start, ident))
        return formatHoverForSymbol(sm, *exportSym, ident);

    if (auto runtimeSym = runtimeMemberSymbolAt(*result->sema, source, start, ident))
        return formatHoverForSymbol(sm, *runtimeSym, ident);

    return {};
}

std::string symbolsForSource(const std::string &source, const std::string &path) {
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};
    uint32_t fileId = sm.addFile(path);
    input.fileId = fileId;

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result || !result->sema)
        return {};

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

    auto types = result->sema->getTypeNames();
    for (const auto &tn : types)
        out << tn << '\t' << "type" << '\t' << tn << '\t' << 0 << '\n';

    return out.str();
}

template <typename Worker> void *startSemanticJob(SemanticJobKind kind, Worker worker) {
    auto job = std::make_shared<SemanticJob>(kind);
    void *handle = semanticJobHandleFor(job);
    if (!handle)
        return nullptr;

    if (!tryAcquireSemanticWorkerSlot()) {
        completeSemanticJobWithError(job, "semantic worker pool busy");
        return handle;
    }

    try {
        std::thread([job, worker = std::move(worker)]() mutable {
            try {
                if (!job->cancelled.load(std::memory_order_acquire))
                    worker(*job);
            } catch (const std::exception &ex) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->error = ex.what();
            } catch (...) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->error = "unknown semantic job failure";
            }
            finishSemanticJob(job);
        }).detach();
    } catch (const std::exception &ex) {
        releaseSemanticWorkerSlot();
        completeSemanticJobWithError(job, ex.what());
    } catch (...) {
        releaseSemanticWorkerSlot();
        completeSemanticJobWithError(job, "failed to start semantic job");
    }
    return handle;
}
} // namespace

extern "C" {
rt_string rt_zia_complete_for_file(rt_string source,
                                   rt_string file_path,
                                   int64_t line,
                                   int64_t col);
void *rt_zia_completion_items_for_file(rt_string source,
                                       rt_string file_path,
                                       int64_t line,
                                       int64_t col);
rt_string rt_zia_signature_help_for_file(rt_string source,
                                         rt_string file_path,
                                         int64_t line,
                                         int64_t col);
void *rt_zia_signature_info_for_file(rt_string source,
                                     rt_string file_path,
                                     int64_t line,
                                     int64_t col);
rt_string rt_zia_check_for_file(rt_string source, rt_string file_path);
rt_string rt_zia_hover_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col);
void *rt_zia_hover_info_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col);
rt_string rt_zia_symbols_for_file(rt_string source, rt_string file_path);
void *rt_zia_toolchain_check_for_file(rt_string source, rt_string file_path);
void *rt_zia_toolchain_compile_for_file(rt_string source, rt_string file_path);
void *rt_zia_completion_begin_items_for_file(rt_string source,
                                             rt_string file_path,
                                             int64_t line,
                                             int64_t col);
void *rt_zia_completion_begin_signature_info_for_file(rt_string source,
                                                      rt_string file_path,
                                                      int64_t line,
                                                      int64_t col);
void *rt_zia_completion_begin_hover_info_for_file(rt_string source,
                                                  rt_string file_path,
                                                  int64_t line,
                                                  int64_t col);
void *rt_zia_completion_begin_symbols_for_file(rt_string source, rt_string file_path);
void *rt_zia_toolchain_begin_check_for_file(rt_string source, rt_string file_path);
int8_t rt_zia_semantic_job_is_done(void *handle);
int8_t rt_zia_semantic_job_is_error(void *handle);
rt_string rt_zia_semantic_job_error(void *handle);
int64_t rt_zia_semantic_job_kind(void *handle);
void rt_zia_semantic_job_cancel(void *handle);
void *rt_zia_semantic_job_completion_items(void *handle);
void *rt_zia_semantic_job_signature_info(void *handle);
void *rt_zia_semantic_job_hover_info(void *handle);
rt_string rt_zia_semantic_job_symbols(void *handle);
void *rt_zia_semantic_job_diagnostics(void *handle);
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
rt_string rt_zia_complete_for_file(rt_string source,
                                   rt_string file_path,
                                   int64_t line,
                                   int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    auto items = s_engine.complete(sourceStr, (int)line, (int)col, pathStr);
    std::string result = serialize(items);

    return rt_string_from_bytes(result.c_str(), result.size());
}

/// @brief Runtime entry point: structured completion items at (@p line, @p col)
///        in @p source. Returns a Seq<Map> with stable item fields.
void *rt_zia_completion_items(rt_string source, int64_t line, int64_t col) {
    std::string sourceStr = toStdString(source);
    auto items = s_engine.complete(sourceStr, (int)line, (int)col);
    return completionItemsToSeq(items);
}

/// @brief As @ref rt_zia_completion_items but with an explicit @p file_path so
///        relative binds and project-aware source locations can be resolved.
void *rt_zia_completion_items_for_file(rt_string source,
                                       rt_string file_path,
                                       int64_t line,
                                       int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    auto items = s_engine.complete(sourceStr, (int)line, (int)col, pathStr);
    return completionItemsToSeq(items);
}

/// @brief Runtime entry point: signature help at (@p line, @p col) with no
///        file context (delegates to @ref rt_zia_signature_help_for_file).
rt_string rt_zia_signature_help(rt_string source, int64_t line, int64_t col) {
    return rt_zia_signature_help_for_file(source, nullptr, line, col);
}

/// @brief Runtime entry point: return call signature text for the invocation
///        active at (@p line, @p col), or an empty string if none resolves.
rt_string rt_zia_signature_help_for_file(rt_string source,
                                         rt_string file_path,
                                         int64_t line,
                                         int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);

    std::string result = s_engine.signatureHelp(sourceStr, (int)line, (int)col, pathStr);
    return rt_string_from_bytes(result.c_str(), result.size());
}

/// @brief Runtime entry point: structured signature help at (@p line, @p col)
///        with no file context.
void *rt_zia_signature_info(rt_string source, int64_t line, int64_t col) {
    return rt_zia_signature_info_for_file(source, nullptr, line, col);
}

/// @brief Runtime entry point: structured signature help map for the active
///        invocation at (@p line, @p col).
void *rt_zia_signature_info_for_file(rt_string source,
                                     rt_string file_path,
                                     int64_t line,
                                     int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);
    std::string result = s_engine.signatureHelp(sourceStr, (int)line, (int)col, pathStr);
    return signatureInfoMap(result, sourceStr, (int)line, (int)col, "", true);
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
        out << sev << '\t' << d.loc.line << '\t' << d.loc.column << '\t' << d.code << '\t'
            << d.message << '\n';
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
// Async semantic jobs — worker threads compute into native C++ records; the
// UI thread later materializes Viper runtime maps/sequences by polling results.
// =========================================================================
void *rt_zia_completion_begin_items_for_file(rt_string source,
                                             rt_string file_path,
                                             int64_t line,
                                             int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);
    return startSemanticJob(
        SemanticJobKind::CompletionItems,
        [sourceStr = std::move(sourceStr), pathStr = std::move(pathStr), line, col](
            SemanticJob &job) {
            CompletionEngine engine;
            auto items = engine.complete(sourceStr, (int)line, (int)col, pathStr);
            std::lock_guard<std::mutex> lock(job.mutex);
            if (!job.cancelled.load(std::memory_order_acquire))
                job.completionItems = std::move(items);
        });
}

void *rt_zia_completion_begin_signature_info_for_file(rt_string source,
                                                      rt_string file_path,
                                                      int64_t line,
                                                      int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);
    return startSemanticJob(
        SemanticJobKind::SignatureInfo,
        [sourceStr = std::move(sourceStr), pathStr = std::move(pathStr), line, col](
            SemanticJob &job) {
            CompletionEngine engine;
            std::string display = engine.signatureHelp(sourceStr, (int)line, (int)col, pathStr);
            std::string documentation =
                declarationDocumentationForName(sourceStr, signatureNameFromDisplay(display));
            std::lock_guard<std::mutex> lock(job.mutex);
            if (!job.cancelled.load(std::memory_order_acquire)) {
                job.signatureDisplay = std::move(display);
                job.signatureSource = sourceStr;
                job.signatureDocumentation = std::move(documentation);
                job.signatureLine = (int)line;
                job.signatureCol = (int)col;
            }
        });
}

void *rt_zia_completion_begin_hover_info_for_file(rt_string source,
                                                  rt_string file_path,
                                                  int64_t line,
                                                  int64_t col) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);
    return startSemanticJob(
        SemanticJobKind::HoverInfo,
        [sourceStr = std::move(sourceStr), pathStr = std::move(pathStr), line, col](
            SemanticJob &job) {
            std::string display = hoverForSource(sourceStr, pathStr, line, col);
            std::string documentation =
                declarationDocumentationForName(sourceStr, hoverNameFromDisplay(display));
            std::lock_guard<std::mutex> lock(job.mutex);
            if (!job.cancelled.load(std::memory_order_acquire)) {
                job.hoverDisplay = std::move(display);
                job.hoverDocumentation = std::move(documentation);
            }
        });
}

void *rt_zia_completion_begin_symbols_for_file(rt_string source, rt_string file_path) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);
    return startSemanticJob(
        SemanticJobKind::Symbols,
        [sourceStr = std::move(sourceStr), pathStr = std::move(pathStr)](SemanticJob &job) {
            std::string symbols = symbolsForSource(sourceStr, pathStr);
            std::lock_guard<std::mutex> lock(job.mutex);
            if (!job.cancelled.load(std::memory_order_acquire))
                job.symbols = std::move(symbols);
        });
}

void *rt_zia_toolchain_begin_check_for_file(rt_string source, rt_string file_path) {
    std::string sourceStr = toStdString(source);
    std::string pathStr = editorPathOrDefault(file_path);
    return startSemanticJob(
        SemanticJobKind::Diagnostics,
        [sourceStr = std::move(sourceStr), pathStr = std::move(pathStr)](SemanticJob &job) {
            auto diagnostics = diagnosticRecordsForSource(sourceStr, pathStr);
            std::lock_guard<std::mutex> lock(job.mutex);
            if (!job.cancelled.load(std::memory_order_acquire))
                job.diagnostics = std::move(diagnostics);
        });
}

int8_t rt_zia_semantic_job_is_done(void *handle) {
    auto job = asSemanticJob(handle);
    return job && job->done.load(std::memory_order_acquire) ? 1 : 0;
}

int8_t rt_zia_semantic_job_is_error(void *handle) {
    auto job = asSemanticJob(handle);
    if (!job || !job->done.load(std::memory_order_acquire))
        return 0;
    std::lock_guard<std::mutex> lock(job->mutex);
    return job->error.empty() ? 0 : 1;
}

rt_string rt_zia_semantic_job_error(void *handle) {
    auto job = asSemanticJob(handle);
    if (!job)
        return rt_str_empty();
    std::lock_guard<std::mutex> lock(job->mutex);
    return toRtString(job->error);
}

int64_t rt_zia_semantic_job_kind(void *handle) {
    auto job = asSemanticJob(handle);
    return job ? static_cast<int64_t>(job->kind) : 0;
}

void rt_zia_semantic_job_cancel(void *handle) {
    auto job = asSemanticJob(handle);
    if (job)
        job->cancelled.store(true, std::memory_order_release);
}

void *rt_zia_semantic_job_completion_items(void *handle) {
    auto job = asSemanticJob(handle);
    if (!job || !job->done.load(std::memory_order_acquire) ||
        job->kind != SemanticJobKind::CompletionItems)
        return rt_seq_new_owned();
    std::lock_guard<std::mutex> lock(job->mutex);
    if (!job->error.empty())
        return rt_seq_new_owned();
    return completionItemsToSeq(job->completionItems);
}

void *rt_zia_semantic_job_signature_info(void *handle) {
    auto job = asSemanticJob(handle);
    if (!job || !job->done.load(std::memory_order_acquire) ||
        job->kind != SemanticJobKind::SignatureInfo)
        return signatureInfoMap("", "", 1, 0, "", false);
    std::lock_guard<std::mutex> lock(job->mutex);
    if (!job->error.empty())
        return signatureInfoMap("", "", 1, 0, "", false);
    return signatureInfoMap(job->signatureDisplay,
                            job->signatureSource,
                            job->signatureLine,
                            job->signatureCol,
                            job->signatureDocumentation,
                            false);
}

void *rt_zia_semantic_job_hover_info(void *handle) {
    auto job = asSemanticJob(handle);
    if (!job || !job->done.load(std::memory_order_acquire) ||
        job->kind != SemanticJobKind::HoverInfo)
        return hoverInfoMap("", "", "", false);
    std::lock_guard<std::mutex> lock(job->mutex);
    if (!job->error.empty())
        return hoverInfoMap("", "", "", false);
    return hoverInfoMap(job->hoverDisplay, "", job->hoverDocumentation, false);
}

rt_string rt_zia_semantic_job_symbols(void *handle) {
    auto job = asSemanticJob(handle);
    if (!job || !job->done.load(std::memory_order_acquire) || job->kind != SemanticJobKind::Symbols)
        return rt_str_empty();
    std::lock_guard<std::mutex> lock(job->mutex);
    if (!job->error.empty())
        return rt_str_empty();
    return toRtString(job->symbols);
}

void *rt_zia_semantic_job_diagnostics(void *handle) {
    auto job = asSemanticJob(handle);
    if (!job || !job->done.load(std::memory_order_acquire) ||
        job->kind != SemanticJobKind::Diagnostics)
        return rt_seq_new_owned();
    std::lock_guard<std::mutex> lock(job->mutex);
    if (!job->error.empty())
        return rt_seq_new_owned();
    return diagnosticRecordsToSeq(job->diagnostics);
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
    while (start > 0 && isIdentByte(sourceStr[start - 1]))
        --start;
    size_t end = pos;
    while (end < sourceStr.size() && isIdentByte(sourceStr[end]))
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
    const ScopedSymbol *scoped = result->sema->findSymbolAtPosition(
        ident, result->fileId, static_cast<uint32_t>(line), static_cast<uint32_t>(col + 1));
    std::string hover;
    if (scoped)
        hover = formatHoverForSymbol(sm, scoped->symbol, ident);
    else if (auto exportSym = moduleExportSymbolAt(*result->sema, sourceStr, start, ident))
        hover = formatHoverForSymbol(sm, *exportSym, ident);
    else if (auto runtimeSym = runtimeMemberSymbolAt(*result->sema, sourceStr, start, ident))
        hover = formatHoverForSymbol(sm, *runtimeSym, ident);
    else
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(hover.c_str(), hover.size());
}

/// @brief Runtime entry point: structured hover info with no file context.
void *rt_zia_hover_info(rt_string source, int64_t line, int64_t col) {
    return rt_zia_hover_info_for_file(source, nullptr, line, col);
}

/// @brief Runtime entry point: structured hover info for the identifier at
///        (@p line, @p col).
void *rt_zia_hover_info_for_file(rt_string source, rt_string file_path, int64_t line, int64_t col) {
    std::string sourceStr = toStdString(source);
    rt_string hover = rt_zia_hover_for_file(source, file_path, line, col);
    std::string hoverText = toStdString(hover);
    rt_string_unref(hover);
    return hoverInfoMap(hoverText, sourceStr, "", true);
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
