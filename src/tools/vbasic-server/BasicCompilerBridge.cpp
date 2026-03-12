//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/vbasic-server/BasicCompilerBridge.cpp
// Purpose: Implementation of the BASIC compiler bridge for the language server.
// Key invariants:
//   - Each analysis call creates a fresh SourceManager
//   - BasicCompletionEngine persists for LRU cache benefits
//   - Hover uses shared TextUtils for cursor extraction
// Ownership/Lifetime:
//   - All returned data is fully owned
// Links: tools/vbasic-server/BasicCompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/vbasic-server/BasicCompilerBridge.hpp"

#include "tools/lsp-common/DiagnosticUtils.hpp"
#include "tools/lsp-common/TextUtils.hpp"

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/BasicAnalysis.hpp"
#include "frontends/basic/BasicCompiler.hpp"
#include "frontends/basic/BasicCompletion.hpp"
#include "frontends/basic/Lexer.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/Token.hpp"
#include "il/io/Serializer.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cctype>

namespace viper::server
{

using namespace il::frontends::basic;

/// @brief Uppercase a string to match BASIC's case-folded identifier convention.
static std::string toUpperStr(const std::string &s)
{
    std::string upper;
    upper.reserve(s.size());
    for (char c : s)
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return upper;
}

// --- Constructor / Destructor ---

BasicCompilerBridge::BasicCompilerBridge()
    : completionEngine_(std::make_unique<BasicCompletionEngine>())
{
}

BasicCompilerBridge::~BasicCompilerBridge() = default;

// --- Analysis ---

std::vector<DiagnosticInfo> BasicCompilerBridge::check(const std::string &source,
                                                       const std::string &path)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = source, .path = path};
    auto result = parseAndAnalyzeBasic(input, sm);
    return extractDiagnostics(result->diagnostics);
}

CompileResult BasicCompilerBridge::compile(const std::string &source, const std::string &path)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = source, .path = path};
    BasicCompilerOptions opts{};

    auto result = compileBasic(input, opts, sm);
    return {result.succeeded(), extractDiagnostics(result.diagnostics)};
}

// --- IDE Features ---

std::vector<CompletionInfo> BasicCompilerBridge::completions(const std::string &source,
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

std::string BasicCompilerBridge::hover(const std::string &source,
                                       int line,
                                       int col,
                                       const std::string &path)
{
    auto ctx = extractIdentifierAtCursor(source, line, col);
    if (!ctx.valid)
        return "";

    // BASIC lexer uppercases all identifiers, so normalize for sema lookup.
    std::string ident = toUpperStr(ctx.identifier);

    il::support::SourceManager sm;
    BasicCompilerInput input{.source = source, .path = path};
    auto result = parseAndAnalyzeBasic(input, sm);
    if (!result->sema)
        return "";

    const auto &sema = *result->sema;

    // Try looking up as a variable
    auto varType = sema.lookupVarType(ident);
    if (varType)
    {
        std::string typeStr;
        switch (*varType)
        {
            case SemanticAnalyzer::Type::Int:
                typeStr = "INTEGER";
                break;
            case SemanticAnalyzer::Type::Float:
                typeStr = "DOUBLE";
                break;
            case SemanticAnalyzer::Type::String:
                typeStr = "STRING";
                break;
            case SemanticAnalyzer::Type::Bool:
                typeStr = "BOOLEAN";
                break;
            case SemanticAnalyzer::Type::ArrayInt:
                typeStr = "INTEGER()";
                break;
            case SemanticAnalyzer::Type::ArrayString:
                typeStr = "STRING()";
                break;
            case SemanticAnalyzer::Type::Object:
            {
                auto cls = sema.lookupObjectClassQName(ident);
                typeStr = cls.value_or("Object");
                break;
            }
            case SemanticAnalyzer::Type::Unknown:
                typeStr = "Variant";
                break;
        }

        std::string md = "```basic\n";
        if (sema.isConstSymbol(ident))
            md += "CONST ";
        else
            md += "DIM ";
        md += ident + " AS " + typeStr + "\n```";
        return md;
    }

    // Try looking up as a procedure
    const auto &procTable = sema.procs();
    auto it = procTable.find(ident);
    if (it != procTable.end())
    {
        const auto &sig = it->second;
        std::string md = "```basic\n";
        if (sig.kind == ProcSignature::Kind::Function)
            md += "FUNCTION ";
        else
            md += "SUB ";
        md += ident;
        md += "(";
        for (size_t i = 0; i < sig.params.size(); ++i)
        {
            if (i > 0)
                md += ", ";
            switch (sig.params[i].type)
            {
                case Type::I64:
                    md += "INTEGER";
                    break;
                case Type::F64:
                    md += "DOUBLE";
                    break;
                case Type::Str:
                    md += "STRING";
                    break;
                case Type::Bool:
                    md += "BOOLEAN";
                    break;
                default:
                    md += "?";
                    break;
            }
            if (sig.params[i].is_array)
                md += "()";
        }
        md += ")";
        if (sig.kind == ProcSignature::Kind::Function && sig.retType)
        {
            md += " AS ";
            switch (*sig.retType)
            {
                case Type::I64:
                    md += "INTEGER";
                    break;
                case Type::F64:
                    md += "DOUBLE";
                    break;
                case Type::Str:
                    md += "STRING";
                    break;
                case Type::Bool:
                    md += "BOOLEAN";
                    break;
                default:
                    md += "?";
                    break;
            }
        }
        md += "\n```";
        return md;
    }

    // Try looking up as a class
    const auto *classInfo = sema.oopIndex().findClass(ident);
    if (classInfo)
    {
        std::string md = "```basic\nCLASS " + classInfo->qualifiedName + "\n```";
        md += "\n\n*" + std::to_string(classInfo->fields.size()) + " fields, " +
              std::to_string(classInfo->methods.size()) + " methods*";
        return md;
    }

    return "";
}

std::vector<SymbolInfo> BasicCompilerBridge::symbols(const std::string &source,
                                                      const std::string &path)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = source, .path = path};
    auto result = parseAndAnalyzeBasic(input, sm);
    if (!result->sema)
        return {};

    const auto &sema = *result->sema;
    std::vector<SymbolInfo> out;

    // Variables
    for (const auto &sym : sema.symbols())
    {
        std::string typeStr = "unknown";
        auto ty = sema.lookupVarType(sym);
        if (ty)
        {
            switch (*ty)
            {
                case SemanticAnalyzer::Type::Int:
                    typeStr = "INTEGER";
                    break;
                case SemanticAnalyzer::Type::Float:
                    typeStr = "DOUBLE";
                    break;
                case SemanticAnalyzer::Type::String:
                    typeStr = "STRING";
                    break;
                case SemanticAnalyzer::Type::Bool:
                    typeStr = "BOOLEAN";
                    break;
                case SemanticAnalyzer::Type::ArrayInt:
                    typeStr = "INTEGER()";
                    break;
                case SemanticAnalyzer::Type::ArrayString:
                    typeStr = "STRING()";
                    break;
                case SemanticAnalyzer::Type::Object:
                {
                    auto cls = sema.lookupObjectClassQName(sym);
                    typeStr = cls.value_or("Object");
                    break;
                }
                case SemanticAnalyzer::Type::Unknown:
                    typeStr = "Variant";
                    break;
            }
        }
        out.push_back({sym, "variable", typeStr, sema.isConstSymbol(sym), false});
    }

    // Procedures
    for (const auto &[name, sig] : sema.procs())
    {
        std::string kind =
            sig.kind == ProcSignature::Kind::Function ? "function" : "function";
        out.push_back({name, kind, "", false, false});
    }

    // Classes
    for (const auto &[name, info] : sema.oopIndex().classes())
    {
        out.push_back({name, "type", info.qualifiedName, false, false});
    }

    return out;
}

// --- Dump ---

std::string BasicCompilerBridge::dumpIL(const std::string &source,
                                         const std::string &path,
                                         bool optimized)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = source, .path = path};
    BasicCompilerOptions opts{};
    // BASIC doesn't have an O1 optimization level in opts, but compileBasic
    // returns a module we can serialize regardless.

    auto result = compileBasic(input, opts, sm);
    if (!result.succeeded())
    {
        std::string err = "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics())
            err += "  " + d.message + "\n";
        return err;
    }

    // Optimization would need the transform pipeline applied separately.
    // For now, return unoptimized IL.
    (void)optimized;
    return il::io::Serializer::toString(result.module);
}

std::string BasicCompilerBridge::dumpAst(const std::string &source, const std::string &path)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = source, .path = path};
    auto result = parseAndAnalyzeBasic(input, sm);
    if (!result->ast)
        return "(no AST produced)";

    AstPrinter printer;
    return printer.dump(*result->ast);
}

std::string BasicCompilerBridge::dumpTokens(const std::string &source, const std::string &path)
{
    il::support::SourceManager sm;
    uint32_t fileId = sm.addFile(path);
    Lexer lexer(source, fileId);

    std::string out;
    while (true)
    {
        Token tok = lexer.next();
        if (tok.kind == TokenKind::EndOfFile)
            break;

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%u:%u", tok.loc.line, tok.loc.column);
        out += buf;
        out += '\t';
        out += tokenKindToString(tok.kind);
        if (!tok.lexeme.empty())
        {
            out += '\t';
            out += tok.lexeme;
        }
        out += '\n';
    }
    return out;
}

} // namespace viper::server
