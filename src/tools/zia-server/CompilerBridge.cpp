//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/zia-server/CompilerBridge.cpp
// Purpose: Implementation of the protocol-agnostic Zia compiler facade.
// Key invariants:
//   - Each analysis call creates a fresh SourceManager (no cross-call state)
//   - CompletionEngine persists for LRU cache benefits
//   - Runtime queries use default ICompilerBridge implementations
// Ownership/Lifetime:
//   - All returned data is fully owned
// Links: tools/zia-server/CompilerBridge.hpp, tools/lsp-common/TextUtils.hpp,
//        tools/lsp-common/DiagnosticUtils.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/zia-server/CompilerBridge.hpp"

#include "tools/lsp-common/DiagnosticUtils.hpp"
#include "tools/lsp-common/TextUtils.hpp"

#include "frontends/zia/Compiler.hpp"
#include "frontends/zia/Lexer.hpp"
#include "frontends/zia/Sema.hpp"
#include "frontends/zia/ZiaAnalysis.hpp"
#include "frontends/zia/ZiaAstPrinter.hpp"
#include "frontends/zia/ZiaCompletion.hpp"
#include "il/io/Serializer.hpp"
#include "runtime/collections/rt_map.h"
#include "runtime/collections/rt_seq.h"
#include "runtime/core/rt_string.h"
#include "runtime/graphics/common/rt_zia_completion.h"
#include "runtime/oop/rt_object.h"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace viper::server {

using namespace il::frontends::zia;

// --- Helpers ---

/// @brief Map a Zia semantic Symbol::Kind to the server's SymbolInfo kind string.
static std::string symbolKindStr(Symbol::Kind k) {
    switch (k) {
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
    return "unknown";
}

/// @brief Append token text with C-style escapes for control characters.
/// @details Token dumps are line-oriented text. Escaping tabs, newlines, carriage
///          returns, backslashes, and other control bytes keeps one token per line
///          and makes the output unambiguous for MCP clients and logs.
/// @param out Destination string receiving escaped text.
/// @param text Raw token spelling from the lexer.
static void appendEscapedTokenText(std::string &out, const std::string &text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (unsigned char c : text) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20u || c == 0x7Fu) {
                    out += "\\x";
                    out.push_back(kHex[c >> 4u]);
                    out.push_back(kHex[c & 0x0Fu]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
}

/// @brief RAII wrapper for runtime strings allocated for editor-service calls.
class RuntimeString {
  public:
    explicit RuntimeString(std::string_view text)
        : value_(rt_string_from_bytes(text.data(), text.size())) {}

    ~RuntimeString() {
        rt_string_unref(value_);
    }

    RuntimeString(const RuntimeString &) = delete;
    RuntimeString &operator=(const RuntimeString &) = delete;

    [[nodiscard]] rt_string get() const {
        return value_;
    }

  private:
    rt_string value_{nullptr};
};

static std::string rtStringToStd(rt_string value) {
    const char *cstr = value ? rt_string_cstr(value) : "";
    const size_t len = value ? static_cast<size_t>(rt_str_len(value)) : 0;
    return std::string(cstr ? cstr : "", len);
}

static std::string mapString(void *map, const char *name) {
    if (!map)
        return {};
    RuntimeString key(name);
    rt_string value = rt_map_get_str(map, key.get());
    std::string out = rtStringToStd(value);
    rt_string_unref(value);
    return out;
}

static int64_t mapInt(void *map, const char *name, int64_t fallback = 0) {
    if (!map)
        return fallback;
    RuntimeString key(name);
    return rt_map_get_int_or(map, key.get(), fallback);
}

static bool mapBool(void *map, const char *name, bool fallback = false) {
    if (!map)
        return fallback;
    RuntimeString key(name);
    return rt_map_get_bool_or(map, key.get(), fallback ? 1 : 0) != 0;
}

static void *mapObject(void *map, const char *name) {
    if (!map)
        return nullptr;
    RuntimeString key(name);
    return rt_map_get(map, key.get());
}

static void releaseRuntimeObject(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static uint32_t toSourceCoord(int64_t value) {
    if (value <= 0)
        return 0;
    if (value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
        return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(value);
}

static int zeroBasedColumnForRuntime(int col) {
    return col > 0 ? col - 1 : 0;
}

static SourceRangeInfo rangeFromMap(void *map,
                                    const char *lineKey,
                                    const char *columnKey,
                                    const char *endLineKey,
                                    const char *endColumnKey) {
    SourceRangeInfo range;
    range.file = mapString(map, "file");
    range.line = toSourceCoord(mapInt(map, lineKey));
    range.column = toSourceCoord(mapInt(map, columnKey));
    range.endLine = toSourceCoord(mapInt(map, endLineKey));
    range.endColumn = toSourceCoord(mapInt(map, endColumnKey));
    return range;
}

static LocationInfo locationFromMap(void *map, bool forceDefinition) {
    LocationInfo location;
    location.range = rangeFromMap(map, "line", "column", "endLine", "endColumn");
    location.name = mapString(map, "name");
    location.kind = mapString(map, "kind");
    location.isDefinition = forceDefinition || mapBool(map, "isDefinition");
    return location;
}

static std::optional<LocationInfo> definitionFromRuntimeMap(void *map) {
    if (!map || !mapBool(map, "found"))
        return std::nullopt;
    return locationFromMap(map, true);
}

static std::vector<LocationInfo> referencesFromRuntimeSeq(void *seq) {
    std::vector<LocationInfo> out;
    const int64_t count = seq ? rt_seq_len(seq) : 0;
    out.reserve(static_cast<size_t>(std::max<int64_t>(count, 0)));
    for (int64_t i = 0; i < count; ++i) {
        void *map = rt_seq_get(seq, i);
        out.push_back(locationFromMap(map, false));
    }
    return out;
}

static RenameResult renameFromRuntimeMap(void *map) {
    RenameResult result;
    result.success = mapBool(map, "success");
    result.reason = mapString(map, "reason");
    void *edits = mapObject(map, "edits");
    const int64_t count = edits ? rt_seq_len(edits) : 0;
    result.edits.reserve(static_cast<size_t>(std::max<int64_t>(count, 0)));
    for (int64_t i = 0; i < count; ++i) {
        void *editMap = rt_seq_get(edits, i);
        TextEditInfo edit;
        edit.range =
            rangeFromMap(editMap, "startLine", "startColumn", "endLine", "endColumn");
        edit.newText = mapString(editMap, "newText");
        result.edits.push_back(std::move(edit));
    }
    return result;
}

static SignatureParameterInfo parameterFromRuntimeMap(void *map) {
    SignatureParameterInfo parameter;
    std::string name = mapString(map, "name");
    std::string type = mapString(map, "type");
    parameter.label = type.empty() ? name : name + ": " + type;
    parameter.documentation = mapString(map, "documentation");
    return parameter;
}

static SignatureInfo signatureFromRuntimeMap(void *map) {
    SignatureInfo signature;
    signature.label = mapString(map, "display");
    signature.documentation = mapString(map, "documentation");
    void *params = mapObject(map, "parameters");
    const int64_t count = params ? rt_seq_len(params) : 0;
    signature.parameters.reserve(static_cast<size_t>(std::max<int64_t>(count, 0)));
    for (int64_t i = 0; i < count; ++i)
        signature.parameters.push_back(parameterFromRuntimeMap(rt_seq_get(params, i)));
    return signature;
}

static SignatureHelpInfo signatureHelpFromRuntimeMap(void *map) {
    SignatureHelpInfo result;
    result.available = mapBool(map, "available");
    result.activeSignature = static_cast<int>(mapInt(map, "activeSignature"));
    result.activeParameter = static_cast<int>(mapInt(map, "activeParameter"));
    if (!result.available)
        return result;

    void *overloads = mapObject(map, "overloads");
    const int64_t count = overloads ? rt_seq_len(overloads) : 0;
    result.signatures.reserve(static_cast<size_t>(std::max<int64_t>(count, 0)));
    for (int64_t i = 0; i < count; ++i)
        result.signatures.push_back(signatureFromRuntimeMap(rt_seq_get(overloads, i)));

    if (result.signatures.empty()) {
        SignatureInfo signature;
        signature.label = mapString(map, "display");
        signature.documentation = mapString(map, "documentation");
        void *params = mapObject(map, "parameters");
        const int64_t paramCount = params ? rt_seq_len(params) : 0;
        signature.parameters.reserve(static_cast<size_t>(std::max<int64_t>(paramCount, 0)));
        for (int64_t i = 0; i < paramCount; ++i)
            signature.parameters.push_back(parameterFromRuntimeMap(rt_seq_get(params, i)));
        result.signatures.push_back(std::move(signature));
    }
    return result;
}

static bool containsCaseInsensitive(std::string_view text, std::string_view needle) {
    if (needle.empty())
        return true;
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    auto it = std::search(text.begin(),
                          text.end(),
                          needle.begin(),
                          needle.end(),
                          [&](char a, char b) { return lower(a) == lower(b); });
    return it != text.end();
}

static SemanticTokenType semanticTypeForToken(const Token &token, TokenKind previous) {
    if (token.isKeyword())
        return SemanticTokenType::Keyword;

    switch (token.kind) {
        case TokenKind::Identifier:
            if (previous == TokenKind::KwFunc)
                return SemanticTokenType::Function;
            if (previous == TokenKind::KwClass || previous == TokenKind::KwStruct ||
                previous == TokenKind::KwType) {
                return previous == TokenKind::KwClass ? SemanticTokenType::Class
                                                      : SemanticTokenType::Type;
            }
            if (previous == TokenKind::KwEnum)
                return SemanticTokenType::Enum;
            if (previous == TokenKind::KwInterface)
                return SemanticTokenType::Interface;
            return SemanticTokenType::Variable;
        case TokenKind::IntegerLiteral:
        case TokenKind::NumberLiteral:
            return SemanticTokenType::Number;
        case TokenKind::StringLiteral:
        case TokenKind::StringStart:
        case TokenKind::StringMid:
        case TokenKind::StringEnd:
            return SemanticTokenType::String;
        default:
            return SemanticTokenType::Operator;
    }
}

// --- Constructor / Destructor ---

CompilerBridge::CompilerBridge() : completionEngine_(std::make_unique<CompletionEngine>()) {
    RuntimeString root(".");
    projectIndex_ = rt_zia_project_index_new(root.get());
}

CompilerBridge::~CompilerBridge() {
    std::lock_guard<std::mutex> lock(projectMutex_);
    if (projectIndex_) {
        rt_zia_project_index_destroy(projectIndex_);
        releaseRuntimeObject(projectIndex_);
        projectIndex_ = nullptr;
    }
}

// --- Analysis ---

std::vector<DiagnosticInfo> CompilerBridge::check(const std::string &source,
                                                  const std::string &path) {
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result) {
        DiagnosticInfo info;
        info.severity = 2;
        info.message = "internal error: Zia analysis did not produce a result";
        info.file = path;
        info.code = "V-LSP-ANALYSIS";
        return {info};
    }
    return extractDiagnostics(result->diagnostics, &sm);
}

CompileResult CompilerBridge::compile(const std::string &source, const std::string &path) {
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = il::frontends::zia::compile(input, opts, sm);
    return {result.succeeded(), extractDiagnostics(result.diagnostics, &sm)};
}

void CompilerBridge::updateDocument(const std::string &path, const std::string &source) {
    std::lock_guard<std::mutex> lock(projectMutex_);
    openDocuments_[path] = source;
    if (!projectIndex_)
        return;
    RuntimeString pathValue(path);
    RuntimeString sourceValue(source);
    (void)rt_zia_project_index_update_file(projectIndex_, pathValue.get(), sourceValue.get());
}

void CompilerBridge::removeDocument(const std::string &path) {
    std::lock_guard<std::mutex> lock(projectMutex_);
    openDocuments_.erase(path);
    if (!projectIndex_)
        return;
    RuntimeString pathValue(path);
    (void)rt_zia_project_index_remove_file(projectIndex_, pathValue.get());
}

bool CompilerBridge::supportsDefinition() const {
    std::lock_guard<std::mutex> lock(projectMutex_);
    return projectIndex_ && rt_zia_project_index_is_valid(projectIndex_) != 0;
}

bool CompilerBridge::supportsReferences() const {
    return supportsDefinition();
}

bool CompilerBridge::supportsRename() const {
    return supportsDefinition();
}

bool CompilerBridge::supportsSignatureHelp() const {
    return true;
}

bool CompilerBridge::supportsWorkspaceSymbols() const {
    return true;
}

bool CompilerBridge::supportsSemanticTokens() const {
    return true;
}

// --- Hover helpers ---

/// @brief Resolved hover information for markdown formatting.
struct HoverResult {
    std::string name;
    std::string
        kind; ///< "variable","parameter","function","method","field","type","module","runtime-class"
    std::string type;      ///< developer-facing semantic type string
    std::string signature; ///< Full signature for functions/methods
    std::string ownerName; ///< Parent type name for members
    bool isFinal{false};
    bool isExtern{false};
};

/// @brief Build a human-readable function signature from AST param names + semantic types.
static std::string buildSignatureFromDecl(const std::vector<Param> &params,
                                          const TypeRef &funcType) {
    auto paramTys = funcType ? funcType->paramTypes() : std::vector<TypeRef>{};
    auto retTy = funcType ? funcType->returnType() : TypeRef{};
    std::string sig = "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0)
            sig += ", ";
        sig += params[i].name + ": ";
        if (i < paramTys.size() && paramTys[i])
            sig += paramTys[i]->toDisplayString();
        else
            sig += "?";
    }
    sig += ")";
    if (retTy && retTy->kind != TypeKindSem::Unit && retTy->kind != TypeKindSem::Void)
        sig += " -> " + retTy->toDisplayString();
    return sig;
}

/// @brief Build a function signature from just the ViperType (no param names).
static std::string buildSignatureFromType(const TypeRef &funcType) {
    if (!funcType || funcType->kind != TypeKindSem::Function)
        return "";
    auto paramTys = funcType->paramTypes();
    auto retTy = funcType->returnType();
    std::string sig = "(";
    for (size_t i = 0; i < paramTys.size(); ++i) {
        if (i > 0)
            sig += ", ";
        sig += paramTys[i] ? paramTys[i]->toDisplayString() : "?";
    }
    sig += ")";
    if (retTy && retTy->kind != TypeKindSem::Unit && retTy->kind != TypeKindSem::Void)
        sig += " -> " + retTy->toDisplayString();
    return sig;
}

/// @brief Resolve a hover target using Sema APIs.
static HoverResult resolveHoverTarget(
    const AnalysisResult &ar, const Sema &sema, const HoverContext &ctx, int line, int col) {
    HoverResult result;

    if (!ctx.dotPrefix.empty()) {
        // ── Dotted expression: resolve prefix, then find member ──
        std::vector<std::string> parts;
        std::string token;
        for (char c : ctx.dotPrefix) {
            if (c == '.') {
                if (!token.empty())
                    parts.push_back(token);
                token.clear();
            } else {
                token += c;
            }
        }
        if (!token.empty())
            parts.push_back(token);

        if (parts.empty())
            return result;

        // Look up first part in globals.
        TypeRef current;
        auto globals = sema.getGlobalSymbols();
        for (const auto &sym : globals) {
            if (sym.name == parts[0]) {
                current = sym.type;
                break;
            }
        }

        // Try position-based lookup (locals, params, class fields).
        if (!current) {
            auto *scoped = sema.findSymbolAtPosition(
                parts[0], ar.fileId, static_cast<uint32_t>(line), static_cast<uint32_t>(col));
            if (scoped)
                current = scoped->symbol.type;
        }

        // Try module alias expansion.
        if (!current) {
            std::string ns = sema.resolveModuleAlias(parts[0]);
            if (!ns.empty()) {
                std::string fullQname = ns;
                for (size_t i = 1; i < parts.size(); ++i)
                    fullQname += "." + parts[i];

                std::string classQname = fullQname + "." + ctx.identifier;
                auto classMembers = sema.getRuntimeMembers(classQname);
                if (!classMembers.empty()) {
                    result.name = classQname;
                    result.kind = "runtime-class";
                    int methods = 0, props = 0;
                    for (const auto &m : classMembers) {
                        if (m.kind == Symbol::Kind::Method)
                            ++methods;
                        else
                            ++props;
                    }
                    result.signature = std::to_string(props) + " properties, " +
                                       std::to_string(methods) + " methods";
                    return result;
                }

                auto members = sema.getRuntimeMembers(fullQname);
                if (!members.empty())
                    current = types::runtimeClass(fullQname);
            }
        }

        // Handle Module type.
        if (current && current->kind == TypeKindSem::Module && !current->name.empty()) {
            std::string fullQname = current->name;
            for (size_t i = 1; i < parts.size(); ++i)
                fullQname += "." + parts[i];

            std::string classQname = fullQname + "." + ctx.identifier;
            if (!sema.getRuntimeMembers(classQname).empty()) {
                result.name = classQname;
                result.kind = "runtime-class";
                return result;
            }

            auto members = sema.getRuntimeMembers(fullQname);
            if (!members.empty())
                current = types::runtimeClass(fullQname);
        }

        // Walk remaining prefix parts via getMembersOf.
        if (current) {
            for (size_t i = 1; i < parts.size(); ++i) {
                auto members = sema.getMembersOf(current);
                bool found = false;
                for (const auto &mem : members) {
                    if (mem.name == parts[i]) {
                        if (mem.type && mem.type->kind == TypeKindSem::Function)
                            current = mem.type->returnType();
                        else
                            current = mem.type;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return result;
            }
        }

        if (!current)
            return result;

        // Search for the identifier in members of the resolved type.
        std::string ownerName;
        if (current->kind == TypeKindSem::Ptr && !current->name.empty())
            ownerName = current->name;
        else if (!current->name.empty())
            ownerName = current->name;

        auto members = sema.getMembersOf(current);
        for (const auto &mem : members) {
            if (mem.name == ctx.identifier) {
                result.name = mem.name;
                result.kind = symbolKindStr(mem.kind);
                result.type = mem.type ? mem.type->toDisplayString() : "";
                result.ownerName = ownerName;
                result.isFinal = mem.isFinal;
                result.isExtern = mem.isExtern;
                if (mem.type && mem.type->kind == TypeKindSem::Function) {
                    if (mem.decl && mem.decl->kind == DeclKind::Method) {
                        auto *md = static_cast<MethodDecl *>(mem.decl);
                        result.signature = buildSignatureFromDecl(md->params, mem.type);
                    } else {
                        result.signature = buildSignatureFromType(mem.type);
                    }
                }
                return result;
            }
        }

        return result;
    }

    // ── No dot prefix: search position-based, then globals, types, module aliases ──

    // 0. Position-based lookup.
    {
        auto *scoped = sema.findSymbolAtPosition(
            ctx.identifier, ar.fileId, static_cast<uint32_t>(line), static_cast<uint32_t>(col));
        if (scoped) {
            result.name = scoped->symbol.name;
            result.kind = symbolKindStr(scoped->symbol.kind);
            result.type = scoped->symbol.type ? scoped->symbol.type->toDisplayString() : "";
            result.isFinal = scoped->symbol.isFinal;
            result.isExtern = scoped->symbol.isExtern;
            result.ownerName = scoped->ownerType;
            if (scoped->symbol.type && scoped->symbol.type->kind == TypeKindSem::Function) {
                if (scoped->symbol.decl && scoped->symbol.decl->kind == DeclKind::Method) {
                    auto *md = static_cast<MethodDecl *>(scoped->symbol.decl);
                    result.signature = buildSignatureFromDecl(md->params, scoped->symbol.type);
                } else if (scoped->symbol.decl && scoped->symbol.decl->kind == DeclKind::Function) {
                    auto *fd = static_cast<FunctionDecl *>(scoped->symbol.decl);
                    result.signature = buildSignatureFromDecl(fd->params, scoped->symbol.type);
                } else {
                    result.signature = buildSignatureFromType(scoped->symbol.type);
                }
            }
            return result;
        }
    }

    // 1. Global symbols.
    auto globals = sema.getGlobalSymbols();
    for (const auto &sym : globals) {
        if (sym.name == ctx.identifier) {
            result.name = sym.name;
            result.kind = symbolKindStr(sym.kind);
            result.type = sym.type ? sym.type->toDisplayString() : "";
            result.isFinal = sym.isFinal;
            result.isExtern = sym.isExtern;
            if (sym.type && sym.type->kind == TypeKindSem::Function) {
                if (sym.decl && sym.decl->kind == DeclKind::Function) {
                    auto *fd = static_cast<FunctionDecl *>(sym.decl);
                    result.signature = buildSignatureFromDecl(fd->params, sym.type);
                } else {
                    result.signature = buildSignatureFromType(sym.type);
                }
            }
            return result;
        }
    }

    // 2. User-defined type names.
    auto typeNames = sema.getTypeNames();
    for (const auto &tn : typeNames) {
        if (tn == ctx.identifier) {
            result.name = tn;
            result.kind = "type";
            result.type = tn;
            return result;
        }
    }

    // 3. Module aliases.
    std::string ns = sema.resolveModuleAlias(ctx.identifier);
    if (!ns.empty()) {
        result.name = ctx.identifier;
        result.kind = "module";
        result.type = ns;
        return result;
    }

    return result;
}

/// @brief Escape text before inserting it into generated Markdown code spans or fences.
/// @details Compiler-provided identifiers and type strings should be well-formed, but hover text is
///          still user-visible markdown. Escaping backticks and replacing control characters keeps
///          a malformed or adversarial symbol spelling from closing a fence or corrupting the LSP
///          hover payload.
static std::string escapeMarkdownCodeText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '`') {
            out += "\\`";
        } else if (static_cast<unsigned char>(c) < 0x20 && c != '\t') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

/// @brief Format hover info as rich markdown.
static std::string formatHoverMarkdown(const HoverResult &info) {
    std::string md;

    if (info.kind == "function") {
        md += "```zia\nfunc " + escapeMarkdownCodeText(info.name);
        if (!info.signature.empty())
            md += escapeMarkdownCodeText(info.signature);
        md += "\n```";
    } else if (info.kind == "method") {
        md += "```zia\nmethod " + escapeMarkdownCodeText(info.name);
        if (!info.signature.empty())
            md += escapeMarkdownCodeText(info.signature);
        md += "\n```";
        if (!info.ownerName.empty())
            md += "\n\n*Member of `" + escapeMarkdownCodeText(info.ownerName) + "`*";
    } else if (info.kind == "variable") {
        md += "```zia\n";
        if (info.isFinal)
            md += "final ";
        else
            md += "var ";
        md += escapeMarkdownCodeText(info.name);
        if (!info.type.empty())
            md += ": " + escapeMarkdownCodeText(info.type);
        md += "\n```";
    } else if (info.kind == "parameter") {
        md += "```zia\n" + escapeMarkdownCodeText(info.name);
        if (!info.type.empty())
            md += ": " + escapeMarkdownCodeText(info.type);
        md += "\n```\n\n*Parameter*";
    } else if (info.kind == "field") {
        md += "```zia\nfield " + escapeMarkdownCodeText(info.name);
        if (!info.type.empty())
            md += ": " + escapeMarkdownCodeText(info.type);
        md += "\n```";
        if (!info.ownerName.empty())
            md += "\n\n*Member of `" + escapeMarkdownCodeText(info.ownerName) + "`*";
    } else if (info.kind == "type") {
        md += "```zia\nclass " + escapeMarkdownCodeText(info.name) + "\n```";
    } else if (info.kind == "module") {
        md += "```zia\nbind " + escapeMarkdownCodeText(info.name) + " = " +
              escapeMarkdownCodeText(info.type) + "\n```\n\n*Module namespace*";
    } else if (info.kind == "runtime-class") {
        md += "```zia\nclass " + escapeMarkdownCodeText(info.name) + "\n```";
        if (!info.signature.empty())
            md += "\n\n*Runtime class — " + escapeMarkdownCodeText(info.signature) + "*";
    } else {
        md += "```zia\n" + escapeMarkdownCodeText(info.name);
        if (!info.type.empty())
            md += ": " + escapeMarkdownCodeText(info.type);
        md += "\n```";
    }

    return md;
}

// --- IDE Features ---

std::optional<LocationInfo> CompilerBridge::definition(const std::string &source,
                                                       int line,
                                                       int col,
                                                       const std::string &path) {
    std::lock_guard<std::mutex> lock(projectMutex_);
    if (!projectIndex_)
        return std::nullopt;
    openDocuments_[path] = source;
    RuntimeString pathValue(path);
    RuntimeString sourceValue(source);
    void *map = rt_zia_project_index_definition(projectIndex_,
                                                pathValue.get(),
                                                sourceValue.get(),
                                                line,
                                                zeroBasedColumnForRuntime(col));
    auto result = definitionFromRuntimeMap(map);
    releaseRuntimeObject(map);
    return result;
}

std::vector<LocationInfo> CompilerBridge::references(const std::string &source,
                                                     int line,
                                                     int col,
                                                     const std::string &path) {
    std::lock_guard<std::mutex> lock(projectMutex_);
    if (!projectIndex_)
        return {};
    openDocuments_[path] = source;
    RuntimeString pathValue(path);
    RuntimeString sourceValue(source);
    void *seq = rt_zia_project_index_references(projectIndex_,
                                                pathValue.get(),
                                                sourceValue.get(),
                                                line,
                                                zeroBasedColumnForRuntime(col));
    auto result = referencesFromRuntimeSeq(seq);
    releaseRuntimeObject(seq);
    return result;
}

RenameResult CompilerBridge::rename(const std::string &source,
                                    int line,
                                    int col,
                                    const std::string &path,
                                    const std::string &newName) {
    std::lock_guard<std::mutex> lock(projectMutex_);
    if (!projectIndex_) {
        RenameResult invalid;
        invalid.reason = "invalid_index";
        return invalid;
    }
    openDocuments_[path] = source;
    RuntimeString pathValue(path);
    RuntimeString sourceValue(source);
    RuntimeString newNameValue(newName);
    void *map = rt_zia_project_index_rename_edits(projectIndex_,
                                                  pathValue.get(),
                                                  sourceValue.get(),
                                                  line,
                                                  zeroBasedColumnForRuntime(col),
                                                  newNameValue.get());
    RenameResult result = renameFromRuntimeMap(map);
    releaseRuntimeObject(map);
    return result;
}

SignatureHelpInfo CompilerBridge::signatureHelp(const std::string &source,
                                                int line,
                                                int col,
                                                const std::string &path) {
    RuntimeString sourceValue(source);
    RuntimeString pathValue(path);
    void *map = rt_zia_signature_info_for_file(
        sourceValue.get(), pathValue.get(), line, zeroBasedColumnForRuntime(col));
    SignatureHelpInfo result = signatureHelpFromRuntimeMap(map);
    releaseRuntimeObject(map);
    return result;
}

std::vector<SymbolInfo> CompilerBridge::workspaceSymbols(const std::string &query) {
    std::vector<std::pair<std::string, std::string>> docs;
    {
        std::lock_guard<std::mutex> lock(projectMutex_);
        docs.reserve(openDocuments_.size());
        for (const auto &[path, source] : openDocuments_)
            docs.emplace_back(path, source);
    }

    std::vector<SymbolInfo> out;
    for (const auto &[path, source] : docs) {
        auto docSymbols = symbols(source, path);
        for (auto &symbol : docSymbols) {
            if (!containsCaseInsensitive(symbol.name, query))
                continue;
            if (symbol.file.empty())
                symbol.file = path;
            out.push_back(std::move(symbol));
        }
    }
    std::sort(out.begin(), out.end(), [](const SymbolInfo &lhs, const SymbolInfo &rhs) {
        if (lhs.name != rhs.name)
            return lhs.name < rhs.name;
        if (lhs.file != rhs.file)
            return lhs.file < rhs.file;
        if (lhs.line != rhs.line)
            return lhs.line < rhs.line;
        return lhs.column < rhs.column;
    });
    return out;
}

std::vector<SemanticTokenInfo> CompilerBridge::semanticTokens(const std::string &source,
                                                              const std::string &path) {
    il::support::DiagnosticEngine diag;
    il::support::SourceManager sm;
    uint32_t fileId = sm.addFile(path);
    Lexer lexer(source, fileId, diag);

    std::vector<SemanticTokenInfo> out;
    TokenKind previous = TokenKind::Eof;
    while (true) {
        Token token = lexer.next();
        if (token.kind == TokenKind::Eof)
            break;
        if (token.kind == TokenKind::Error || token.text.empty())
            continue;

        SemanticTokenInfo info;
        info.line = token.loc.line;
        info.column = token.loc.column;
        info.length = static_cast<uint32_t>(
            std::min<size_t>(token.text.size(), std::numeric_limits<uint32_t>::max()));
        info.type = semanticTypeForToken(token, previous);
        out.push_back(info);
        previous = token.kind;
    }
    return out;
}

std::vector<CompletionInfo> CompilerBridge::completions(const std::string &source,
                                                        int line,
                                                        int col,
                                                        const std::string &path) {
    std::lock_guard<std::mutex> lock(completionMutex_);
    auto items = completionEngine_->complete(source, line, col, path);
    std::vector<CompletionInfo> result;
    result.reserve(items.size());
    for (const auto &item : items) {
        result.push_back({item.label,
                          item.insertText,
                          static_cast<int>(item.kind),
                          item.detail,
                          item.sortPriority});
    }
    return result;
}

std::string CompilerBridge::hover(const std::string &source,
                                  int line,
                                  int col,
                                  const std::string &path) {
    auto ctx = extractIdentifierAtCursor(source, line, col);
    if (!ctx.valid)
        return "";

    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result || !result->sema)
        return "";

    auto hoverResult = resolveHoverTarget(*result, *result->sema, ctx, line, col);
    if (hoverResult.name.empty())
        return "";

    return formatHoverMarkdown(hoverResult);
}

std::vector<SymbolInfo> CompilerBridge::symbols(const std::string &source,
                                                const std::string &path) {
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result || !result->sema)
        return {};

    std::vector<SymbolInfo> out;
    const uint32_t mainFileId = result->fileId;

    auto globals = result->sema->getGlobalSymbols();
    for (const auto &sym : globals) {
        if (!sym.decl || sym.decl->loc.file_id != mainFileId)
            continue;
        out.push_back({sym.name,
                       symbolKindStr(sym.kind),
                       sym.type ? sym.type->toDisplayString() : "unknown",
                       sym.isFinal,
                       sym.isExtern,
                       sym.decl->loc.line,
                       sym.decl->loc.column,
                       path});
    }

    auto types = result->sema->getTypeNames();
    for (const auto &tn : types) {
        const size_t pos = source.find(tn);
        if (pos == std::string::npos)
            continue;
        uint32_t typeLine = 1;
        uint32_t typeColumn = 1;
        for (size_t i = 0; i < pos; ++i) {
            if (source[i] == '\n') {
                ++typeLine;
                typeColumn = 1;
            } else {
                ++typeColumn;
            }
        }
        out.push_back({tn, "type", tn, false, false, typeLine, typeColumn, path});
    }

    return out;
}

// --- Dump ---

std::string CompilerBridge::dumpIL(const std::string &source,
                                   const std::string &path,
                                   bool optimized) {
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};
    if (optimized)
        opts.optLevel = OptLevel::O1;

    auto result = il::frontends::zia::compile(input, opts, sm);
    if (!result.succeeded()) {
        std::string err = "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics())
            err += "  " + d.message + "\n";
        return err;
    }

    return il::io::Serializer::toString(result.module);
}

std::string CompilerBridge::dumpAst(const std::string &source, const std::string &path) {
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result || !result->ast)
        return "(no AST produced)";

    ZiaAstPrinter printer;
    return printer.dump(*result->ast);
}

std::string CompilerBridge::dumpTokens(const std::string &source, const std::string &path) {
    il::support::DiagnosticEngine diag;
    il::support::SourceManager sm;
    uint32_t fileId = sm.addFile(path);
    Lexer lexer(source, fileId, diag);

    std::string out;
    while (true) {
        Token tok = lexer.next();
        if (tok.kind == TokenKind::Eof)
            break;

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%u:%u", tok.loc.line, tok.loc.column);
        out += buf;
        out += '\t';
        appendEscapedTokenText(out, tok.text);
        out += '\n';
    }
    return out;
}

} // namespace viper::server
