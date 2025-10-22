//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SemanticAnalyzer.cpp
// Purpose: Define shared utilities and accessors for the BASIC semantic
//          analyzer core.
// Key invariants: Symbol tables stay synchronized with scope tracking and the
//                 helper routines remain internal to the BASIC frontend.
// Ownership/Lifetime: The analyzer borrows diagnostics and AST nodes; state
//                     containers live alongside the analyzer instance.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements shared helpers for the BASIC semantic analyzer façade.
/// @details The translation unit contains constructors, common accessors, and
///          free helpers that support semantic analysis without exposing the
///          intricate bookkeeping to other parts of the frontend.  Utilities in
///          the `semantic_analyzer_detail` namespace assist higher-level
///          semantic passes by formatting diagnostics and translating between
///          AST and analyzer-level abstractions.

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

/// @brief Build a semantic analyzer wired to the shared diagnostic emitter.
/// @details The constructor initialises the procedure registry with the shared
///          diagnostic emitter so newly discovered procedures can report errors
///          through the same channel as expression analysis.  All additional
///          state containers start empty, mirroring a fresh analysis session.
/// @param emitter Diagnostic sink that accumulates semantic errors.
SemanticAnalyzer::SemanticAnalyzer(DiagnosticEmitter &emitter)
    : de(emitter), procReg_(de)
{
}

/// @brief Inspect the set of canonical symbols encountered so far.
/// @details The returned view mirrors the analyzer's internal symbol table and
///          remains valid as long as the analyzer outlives the caller.  It is
///          primarily used by tests and debugging tools to assert symbol
///          discovery.  The set should be treated as read-only.
/// @return Reference to the analyzer's symbol table.
const std::unordered_set<std::string> &SemanticAnalyzer::symbols() const
{
    return symbols_;
}

/// @brief Retrieve the set of statement labels defined in the analysed program.
/// @details Labels are collected while visiting statement nodes.  Consumers can
///          query the set to validate label reachability or check for unused
///          labels after analysis completes.
/// @return Reference to the label-definition set tracked by the analyzer.
const std::unordered_set<int> &SemanticAnalyzer::labels() const
{
    return labels_;
}

/// @brief Expose the set of statement labels referenced by control-flow ops.
/// @details The analyzer records label references when parsing GOTO/GOSUB and
///          similar constructs.  This accessor exists so downstream passes can
///          confirm that each reference resolves to a known label.
/// @return Reference to the set of observed label references.
const std::unordered_set<int> &SemanticAnalyzer::labelRefs() const
{
    return labelRefs_;
}

/// @brief Access the registered procedure table for the current analysis run.
/// @details The procedure registry aggregates SUB/FUNCTION metadata discovered
///          while walking the program.  Returning the registry view allows other
///          stages to resolve declarations or inspect parameters without taking
///          ownership of the underlying data.
/// @return Procedure table maintained by the analyzer.
const ProcTable &SemanticAnalyzer::procs() const
{
    return procReg_.procs();
}

/// @brief Look up the inferred semantic type of a variable by name.
/// @details The analyzer assigns default types based on suffix conventions and
///          records explicit declarations as they appear.  This helper reports
///          the current understanding of a symbol's type so callers can emit
///          diagnostics or generate code accordingly.
/// @param name Canonical symbol identifier (after scope resolution).
/// @return Optional semantic type when the variable has been seen; otherwise
///         std::nullopt.
std::optional<SemanticAnalyzer::Type>
SemanticAnalyzer::lookupVarType(const std::string &name) const
{
    if (auto it = varTypes_.find(name); it != varTypes_.end())
        return it->second;
    return std::nullopt;
}

/// @brief Resolve a symbol name through the current scope and track its type.
/// @details The helper first normalises the requested identifier using the
///          scope stack so shadowing and aliasing follow BASIC rules.  When the
///          symbol represents a definition rather than a reference it is
///          inserted into the global symbol table, default types are assigned
///          based on suffix heuristics, and procedure scopes are notified of the
///          change so they can drive diagnostics such as shadowed declarations.
/// @param name Symbol to resolve and potentially canonicalise (mutated in
///             place).
/// @param kind Describes how the symbol participates (definition, reference,
///             input target, etc.).
void SemanticAnalyzer::resolveAndTrackSymbol(std::string &name, SymbolKind kind)
{
    if (auto mapped = scopes_.resolve(name))
        name = *mapped;

    if (kind == SymbolKind::Reference)
        return;

    auto insertResult = symbols_.insert(name);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteSymbolInserted(name);

    const bool forceDefault = kind == SymbolKind::InputTarget;
    auto itType = varTypes_.find(name);
    if (forceDefault || itType == varTypes_.end())
    {
        Type defaultType = Type::Int;
        if (!name.empty())
        {
            if (name.back() == '$')
                defaultType = Type::String;
            else if (name.back() == '#' || name.back() == '!')
                defaultType = Type::Float;
        }
        if (activeProcScope_)
        {
            std::optional<Type> previous;
            if (itType != varTypes_.end())
                previous = itType->second;
            activeProcScope_->noteVarTypeMutation(name, previous);
        }
        varTypes_[name] = defaultType;
    }
}

} // namespace il::frontends::basic

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Compute the Levenshtein distance between two ASCII strings.
/// @details The implementation uses a rolling dynamic-programming table to
///          reduce memory consumption while still providing the exact edit
///          distance.  This is used to suggest identifier spellings in
///          diagnostics by ranking candidate names against user input.
/// @param a First string to compare.
/// @param b Second string to compare.
/// @return Minimum number of single-character edits required to transform
///         @p a into @p b.
size_t levenshtein(const std::string &a, const std::string &b)
{
    const size_t m = a.size();
    const size_t n = b.size();
    std::vector<size_t> prev(n + 1), cur(n + 1);
    for (size_t j = 0; j <= n; ++j)
        prev[j] = j;
    for (size_t i = 1; i <= m; ++i)
    {
        cur[0] = i;
        for (size_t j = 1; j <= n; ++j)
        {
            size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[n];
}

/// @brief Translate an AST type enumerator into a semantic analyzer type.
/// @details The semantic analyzer exposes its own coarse-grained type system
///          which intentionally diverges from the AST enumeration.  This helper
///          normalises AST categories into the semantic representation so the
///          analyzer can reason about type compatibility without depending on
///          concrete AST definitions.
/// @param ty AST type enumerator.
/// @return Corresponding semantic analyzer type (defaults to Int when
///         unrecognised).
SemanticAnalyzer::Type astToSemanticType(::il::frontends::basic::Type ty)
{
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return SemanticAnalyzer::Type::Int;
        case ::il::frontends::basic::Type::F64:
            return SemanticAnalyzer::Type::Float;
        case ::il::frontends::basic::Type::Str:
            return SemanticAnalyzer::Type::String;
        case ::il::frontends::basic::Type::Bool:
            return SemanticAnalyzer::Type::Bool;
    }
    return SemanticAnalyzer::Type::Int;
}

/// @brief Look up the canonical display name for a builtin invocation.
/// @details The frontend registers builtin metadata that includes user-facing
///          names.  This helper retrieves the name to feed into diagnostics and
///          tracing output without duplicating registry logic.
/// @param b Builtin enumerator describing the intrinsic being invoked.
/// @return Null-terminated string naming the builtin.
const char *builtinName(BuiltinCallExpr::Builtin b)
{
    return getBuiltinInfo(b).name;
}

/// @brief Provide a human-readable representation of a semantic analyzer type.
/// @details Converts enum values into descriptive strings for diagnostics and
///          debug output.  Unknown values fall back to "UNKNOWN" to avoid
///          crashing in edge cases while still signalling unexpected input.
/// @param type Semantic analyzer type to stringify.
/// @return Static string describing @p type.
const char *semanticTypeName(SemanticAnalyzer::Type type)
{
    using Type = SemanticAnalyzer::Type;
    switch (type)
    {
        case Type::Int:
            return "INT";
        case Type::Float:
            return "FLOAT";
        case Type::String:
            return "STRING";
        case Type::Bool:
            return "BOOLEAN";
        case Type::ArrayInt:
            return "ARRAY(INT)";
        case Type::Unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

/// @brief Format a BASIC logical operator into a display string.
/// @details The parser encodes both short-circuiting and eager boolean
///          operators using the same enum.  This helper maps each variant to the
///          textual keyword used in diagnostics so error messages read naturally.
/// @param op Logical operator enumerator.
/// @return Keyword string for @p op or "<logical>" when unknown.
const char *logicalOpName(BinaryExpr::Op op)
{
    switch (op)
    {
        case BinaryExpr::Op::LogicalAndShort:
            return "ANDALSO";
        case BinaryExpr::Op::LogicalOrShort:
            return "ORELSE";
        case BinaryExpr::Op::LogicalAnd:
            return "AND";
        case BinaryExpr::Op::LogicalOr:
            return "OR";
        default:
            break;
    }
    return "<logical>";
}

/// @brief Produce textual context for a conditional expression in diagnostics.
/// @details Attempts to serialise common expression forms (variables, literals,
///          and string constants) into a short string suitable for embedding in
///          error messages.  Expressions that are not easily rendered fall back
///          to an empty string so callers can omit the detail gracefully.
/// @param expr AST expression describing a condition.
/// @return String representation of the expression, or empty when not
///         representable.
std::string conditionExprText(const Expr &expr)
{
    if (auto *var = dynamic_cast<const VarExpr *>(&expr))
        return var->name;
    if (auto *intExpr = dynamic_cast<const IntExpr *>(&expr))
        return std::to_string(intExpr->value);
    if (auto *floatExpr = dynamic_cast<const FloatExpr *>(&expr))
    {
        std::ostringstream oss;
        oss << floatExpr->value;
        return oss.str();
    }
    if (auto *boolExpr = dynamic_cast<const BoolExpr *>(&expr))
        return boolExpr->value ? "TRUE" : "FALSE";
    if (auto *strExpr = dynamic_cast<const StringExpr *>(&expr))
    {
        std::string text = "\"";
        text += strExpr->value;
        text += '"';
        return text;
    }
    return {};
}

} // namespace il::frontends::basic::semantic_analyzer_detail
