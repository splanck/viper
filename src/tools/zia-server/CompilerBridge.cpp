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
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <sstream>

namespace viper::server
{

using namespace il::frontends::zia;

// --- Helpers ---

static std::string symbolKindStr(Symbol::Kind k)
{
    switch (k)
    {
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

// --- Constructor / Destructor ---

CompilerBridge::CompilerBridge() : completionEngine_(std::make_unique<CompletionEngine>()) {}

CompilerBridge::~CompilerBridge() = default;

// --- Analysis ---

std::vector<DiagnosticInfo> CompilerBridge::check(const std::string &source,
                                                  const std::string &path)
{
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    return extractDiagnostics(result->diagnostics);
}

CompileResult CompilerBridge::compile(const std::string &source, const std::string &path)
{
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = il::frontends::zia::compile(input, opts, sm);
    return {result.succeeded(), extractDiagnostics(result.diagnostics)};
}

// --- Hover helpers ---

/// @brief Resolved hover information for markdown formatting.
struct HoverResult
{
    std::string name;
    std::string
        kind; ///< "variable","parameter","function","method","field","type","module","runtime-class"
    std::string type;      ///< ViperType::toString()
    std::string signature; ///< Full signature for functions/methods
    std::string ownerName; ///< Parent type name for members
    bool isFinal{false};
    bool isExtern{false};
};

/// @brief Build a human-readable function signature from AST param names + semantic types.
static std::string buildSignatureFromDecl(const std::vector<Param> &params, const TypeRef &funcType)
{
    auto paramTys = funcType ? funcType->paramTypes() : std::vector<TypeRef>{};
    auto retTy = funcType ? funcType->returnType() : TypeRef{};
    std::string sig = "(";
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (i > 0)
            sig += ", ";
        sig += params[i].name + ": ";
        if (i < paramTys.size() && paramTys[i])
            sig += paramTys[i]->toString();
        else
            sig += "?";
    }
    sig += ")";
    if (retTy && retTy->kind != TypeKindSem::Unit && retTy->kind != TypeKindSem::Void)
        sig += " -> " + retTy->toString();
    return sig;
}

/// @brief Build a function signature from just the ViperType (no param names).
static std::string buildSignatureFromType(const TypeRef &funcType)
{
    if (!funcType || funcType->kind != TypeKindSem::Function)
        return "";
    auto paramTys = funcType->paramTypes();
    auto retTy = funcType->returnType();
    std::string sig = "(";
    for (size_t i = 0; i < paramTys.size(); ++i)
    {
        if (i > 0)
            sig += ", ";
        sig += paramTys[i] ? paramTys[i]->toString() : "?";
    }
    sig += ")";
    if (retTy && retTy->kind != TypeKindSem::Unit && retTy->kind != TypeKindSem::Void)
        sig += " -> " + retTy->toString();
    return sig;
}

/// @brief Resolve a hover target using Sema APIs.
static HoverResult resolveHoverTarget(const AnalysisResult &ar,
                                      const Sema &sema,
                                      const HoverContext &ctx,
                                      int line,
                                      int col)
{
    HoverResult result;

    if (!ctx.dotPrefix.empty())
    {
        // ── Dotted expression: resolve prefix, then find member ──
        std::vector<std::string> parts;
        std::string token;
        for (char c : ctx.dotPrefix)
        {
            if (c == '.')
            {
                if (!token.empty())
                    parts.push_back(token);
                token.clear();
            }
            else
            {
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
        for (const auto &sym : globals)
        {
            if (sym.name == parts[0])
            {
                current = sym.type;
                break;
            }
        }

        // Try position-based lookup (locals, params, entity fields).
        if (!current)
        {
            auto *scoped = sema.findSymbolAtPosition(
                parts[0], ar.fileId, static_cast<uint32_t>(line), static_cast<uint32_t>(col));
            if (scoped)
                current = scoped->symbol.type;
        }

        // Try module alias expansion.
        if (!current)
        {
            std::string ns = sema.resolveModuleAlias(parts[0]);
            if (!ns.empty())
            {
                std::string fullQname = ns;
                for (size_t i = 1; i < parts.size(); ++i)
                    fullQname += "." + parts[i];

                std::string classQname = fullQname + "." + ctx.identifier;
                auto classMembers = sema.getRuntimeMembers(classQname);
                if (!classMembers.empty())
                {
                    result.name = classQname;
                    result.kind = "runtime-class";
                    int methods = 0, props = 0;
                    for (const auto &m : classMembers)
                    {
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
        if (current && current->kind == TypeKindSem::Module && !current->name.empty())
        {
            std::string fullQname = current->name;
            for (size_t i = 1; i < parts.size(); ++i)
                fullQname += "." + parts[i];

            std::string classQname = fullQname + "." + ctx.identifier;
            if (!sema.getRuntimeMembers(classQname).empty())
            {
                result.name = classQname;
                result.kind = "runtime-class";
                return result;
            }

            auto members = sema.getRuntimeMembers(fullQname);
            if (!members.empty())
                current = types::runtimeClass(fullQname);
        }

        // Walk remaining prefix parts via getMembersOf.
        if (current)
        {
            for (size_t i = 1; i < parts.size(); ++i)
            {
                auto members = sema.getMembersOf(current);
                bool found = false;
                for (const auto &mem : members)
                {
                    if (mem.name == parts[i])
                    {
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
        for (const auto &mem : members)
        {
            if (mem.name == ctx.identifier)
            {
                result.name = mem.name;
                result.kind = symbolKindStr(mem.kind);
                result.type = mem.type ? mem.type->toString() : "";
                result.ownerName = ownerName;
                result.isFinal = mem.isFinal;
                result.isExtern = mem.isExtern;
                if (mem.type && mem.type->kind == TypeKindSem::Function)
                {
                    if (mem.decl && mem.decl->kind == DeclKind::Method)
                    {
                        auto *md = static_cast<MethodDecl *>(mem.decl);
                        result.signature = buildSignatureFromDecl(md->params, mem.type);
                    }
                    else
                    {
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
        if (scoped)
        {
            result.name = scoped->symbol.name;
            result.kind = symbolKindStr(scoped->symbol.kind);
            result.type = scoped->symbol.type ? scoped->symbol.type->toString() : "";
            result.isFinal = scoped->symbol.isFinal;
            result.isExtern = scoped->symbol.isExtern;
            result.ownerName = scoped->ownerType;
            if (scoped->symbol.type && scoped->symbol.type->kind == TypeKindSem::Function)
            {
                if (scoped->symbol.decl && scoped->symbol.decl->kind == DeclKind::Method)
                {
                    auto *md = static_cast<MethodDecl *>(scoped->symbol.decl);
                    result.signature = buildSignatureFromDecl(md->params, scoped->symbol.type);
                }
                else if (scoped->symbol.decl && scoped->symbol.decl->kind == DeclKind::Function)
                {
                    auto *fd = static_cast<FunctionDecl *>(scoped->symbol.decl);
                    result.signature = buildSignatureFromDecl(fd->params, scoped->symbol.type);
                }
                else
                {
                    result.signature = buildSignatureFromType(scoped->symbol.type);
                }
            }
            return result;
        }
    }

    // 1. Global symbols.
    auto globals = sema.getGlobalSymbols();
    for (const auto &sym : globals)
    {
        if (sym.name == ctx.identifier)
        {
            result.name = sym.name;
            result.kind = symbolKindStr(sym.kind);
            result.type = sym.type ? sym.type->toString() : "";
            result.isFinal = sym.isFinal;
            result.isExtern = sym.isExtern;
            if (sym.type && sym.type->kind == TypeKindSem::Function)
            {
                if (sym.decl && sym.decl->kind == DeclKind::Function)
                {
                    auto *fd = static_cast<FunctionDecl *>(sym.decl);
                    result.signature = buildSignatureFromDecl(fd->params, sym.type);
                }
                else
                {
                    result.signature = buildSignatureFromType(sym.type);
                }
            }
            return result;
        }
    }

    // 2. User-defined type names.
    auto typeNames = sema.getTypeNames();
    for (const auto &tn : typeNames)
    {
        if (tn == ctx.identifier)
        {
            result.name = tn;
            result.kind = "type";
            result.type = tn;
            return result;
        }
    }

    // 3. Module aliases.
    std::string ns = sema.resolveModuleAlias(ctx.identifier);
    if (!ns.empty())
    {
        result.name = ctx.identifier;
        result.kind = "module";
        result.type = ns;
        return result;
    }

    return result;
}

/// @brief Format hover info as rich markdown.
static std::string formatHoverMarkdown(const HoverResult &info)
{
    std::string md;

    if (info.kind == "function")
    {
        md += "```zia\nfunc " + info.name;
        if (!info.signature.empty())
            md += info.signature;
        md += "\n```";
    }
    else if (info.kind == "method")
    {
        md += "```zia\nmethod " + info.name;
        if (!info.signature.empty())
            md += info.signature;
        md += "\n```";
        if (!info.ownerName.empty())
            md += "\n\n*Member of `" + info.ownerName + "`*";
    }
    else if (info.kind == "variable")
    {
        md += "```zia\n";
        if (info.isFinal)
            md += "final ";
        else
            md += "var ";
        md += info.name;
        if (!info.type.empty())
            md += ": " + info.type;
        md += "\n```";
    }
    else if (info.kind == "parameter")
    {
        md += "```zia\n" + info.name;
        if (!info.type.empty())
            md += ": " + info.type;
        md += "\n```\n\n*Parameter*";
    }
    else if (info.kind == "field")
    {
        md += "```zia\nfield " + info.name;
        if (!info.type.empty())
            md += ": " + info.type;
        md += "\n```";
        if (!info.ownerName.empty())
            md += "\n\n*Member of `" + info.ownerName + "`*";
    }
    else if (info.kind == "type")
    {
        md += "```zia\nentity " + info.name + "\n```";
    }
    else if (info.kind == "module")
    {
        md += "```zia\nbind " + info.name + " = " + info.type + "\n```\n\n*Module namespace*";
    }
    else if (info.kind == "runtime-class")
    {
        md += "```zia\nclass " + info.name + "\n```";
        if (!info.signature.empty())
            md += "\n\n*Runtime class — " + info.signature + "*";
    }
    else
    {
        md += "```zia\n" + info.name;
        if (!info.type.empty())
            md += ": " + info.type;
        md += "\n```";
    }

    return md;
}

// --- IDE Features ---

std::vector<CompletionInfo> CompilerBridge::completions(const std::string &source,
                                                        int line,
                                                        int col,
                                                        const std::string &path)
{
    auto items = completionEngine_->complete(source, line, col, path);
    std::vector<CompletionInfo> result;
    result.reserve(items.size());
    for (const auto &item : items)
    {
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
                                  const std::string &path)
{
    auto ctx = extractIdentifierAtCursor(source, line, col);
    if (!ctx.valid)
        return "";

    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result->sema)
        return "";

    auto hoverResult = resolveHoverTarget(*result, *result->sema, ctx, line, col);
    if (hoverResult.name.empty())
        return "";

    return formatHoverMarkdown(hoverResult);
}

std::vector<SymbolInfo> CompilerBridge::symbols(const std::string &source, const std::string &path)
{
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    uint32_t mainFileId = sm.addFile(std::string(path));
    input.fileId = mainFileId;

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result->sema)
        return {};

    std::vector<SymbolInfo> out;

    auto globals = result->sema->getGlobalSymbols();
    for (const auto &sym : globals)
    {
        if (!sym.decl || sym.decl->loc.file_id != mainFileId)
            continue;
        out.push_back({sym.name,
                       symbolKindStr(sym.kind),
                       sym.type ? sym.type->toString() : "unknown",
                       sym.isFinal,
                       sym.isExtern});
    }

    auto types = result->sema->getTypeNames();
    for (const auto &tn : types)
    {
        out.push_back({tn, "type", tn, false, false});
    }

    return out;
}

// --- Dump ---

std::string CompilerBridge::dumpIL(const std::string &source,
                                   const std::string &path,
                                   bool optimized)
{
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};
    if (optimized)
        opts.optLevel = OptLevel::O1;

    auto result = il::frontends::zia::compile(input, opts, sm);
    if (!result.succeeded())
    {
        std::string err = "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics())
            err += "  " + d.message + "\n";
        return err;
    }

    return il::io::Serializer::toString(result.module);
}

std::string CompilerBridge::dumpAst(const std::string &source, const std::string &path)
{
    il::support::SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};

    auto result = parseAndAnalyze(input, opts, sm);
    if (!result->ast)
        return "(no AST produced)";

    ZiaAstPrinter printer;
    return printer.dump(*result->ast);
}

std::string CompilerBridge::dumpTokens(const std::string &source, const std::string &path)
{
    il::support::DiagnosticEngine diag;
    il::support::SourceManager sm;
    uint32_t fileId = sm.addFile(path);
    Lexer lexer(source, fileId, diag);

    std::string out;
    while (true)
    {
        Token tok = lexer.next();
        if (tok.kind == TokenKind::Eof)
            break;

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%u:%u", tok.loc.line, tok.loc.column);
        out += buf;
        out += '\t';
        out += tok.text;
        out += '\n';
    }
    return out;
}

} // namespace viper::server
