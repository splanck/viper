//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the concrete implementation of the BASIC semantic analyzer's
// shared helpers. The functions in this unit construct the analyzer, expose
// accessors used by the parser and lowerer, and house utility routines for
// symbol canonicalisation, diagnostics, and suggestion ranking.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements common helpers for the BASIC semantic analyzer.
/// @details The analyzer spans multiple translation units; this file carries the
///          utilities that do not depend on specialized checks. Keeping them in
///          one location simplifies dependency management for both the parser
///          and lowering pipelines.

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

/// @brief Construct the analyzer around a diagnostic emitter.
///
/// @details The analyzer reuses a shared diagnostic emitter to report semantic
///          issues. The constructor also initialises the procedure registry that
///          manages declared subs/functions, ensuring diagnostics have access to
///          consistent state while analyses run.
///
/// @param emitter Diagnostic sink receiving semantic error reports.
SemanticAnalyzer::SemanticAnalyzer(DiagnosticEmitter &emitter)
    : de(emitter), procReg_(de)
{
}

/// @brief Obtain the set of known variable names.
///
/// @details The returned reference exposes the analyzer's canonicalised symbol
///          names, including implicitly declared identifiers. Callers use this
///          to drive lowering decisions and diagnostics without copying data.
///
/// @return Reference to the analyzer's symbol set.
const std::unordered_set<std::string> &SemanticAnalyzer::symbols() const
{
    return symbols_;
}

/// @brief Access the set of declared labels.
///
/// @details The parser records labels as integers. Exposing the set lets
///          diagnostics validate forward references and unused labels once the
///          program has been analysed.
///
/// @return Reference to the label set collected during analysis.
const std::unordered_set<int> &SemanticAnalyzer::labels() const
{
    return labels_;
}

/// @brief Access the set of referenced labels.
///
/// @details Tracks which labels appear in control flow statements. Comparing it
///          against `labels()` helps detect undefined or unused labels.
///
/// @return Reference to the referenced label set.
const std::unordered_set<int> &SemanticAnalyzer::labelRefs() const
{
    return labelRefs_;
}

/// @brief Retrieve the table of known procedures.
///
/// @details The procedure registry encapsulates metadata about subs/functions
///          discovered during analysis. Consumers (such as the lowerer) use it
///          to query signatures when emitting calls.
///
/// @return Reference to the immutable procedure table.
const ProcTable &SemanticAnalyzer::procs() const
{
    return procReg_.procs();
}

/// @brief Lookup the resolved type for a variable.
///
/// @details Variables may inherit types from suffixes or explicit declarations.
///          This helper searches the analyzer's map and returns an optional type
///          to allow callers to distinguish between known and unknown symbols.
///
/// @param name Canonicalised variable name to query.
/// @return Optional containing the resolved type when known.
std::optional<SemanticAnalyzer::Type>
SemanticAnalyzer::lookupVarType(const std::string &name) const
{
    if (auto it = varTypes_.find(name); it != varTypes_.end())
        return it->second;
    return std::nullopt;
}

/// @brief Canonicalise a symbol name and ensure analyzer state tracks it.
///
/// @details Resolves scoped aliases, inserts the symbol into the tracked set
///          when appropriate, and applies BASIC's default type inference rules
///          (suffix-based) unless an explicit declaration already exists. Input
///          targets force default typing because runtime conversions expect a
///          concrete type. Procedure scopes capture mutations for later undo if
///          semantic analysis fails.
///
/// @param name Symbol name to canonicalise; updated in-place when scopes remap.
/// @param kind Indicates how the symbol participates (declaration, reference,
///             etc.), influencing whether defaults are applied.
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

/// @brief Compute the Levenshtein edit distance between two strings.
///
/// @details Uses a dynamic programming formulation with two rolling buffers to
///          reduce memory use while computing the insertion/deletion/substitution
///          cost. The helper supports identifier suggestion diagnostics when the
///          user mistypes a symbol name.
///
/// @param a First string to compare.
/// @param b Second string to compare.
/// @return Minimum number of single-character edits to transform @p a into @p b.
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

/// @brief Convert a BASIC AST type to the semantic analyzer's internal type.
///
/// @details The semantic layer uses its own enumeration to describe inferred
///          types. This helper bridges AST-level types to that enumeration,
///          providing a single point of mapping for scalars.
///
/// @param ty BASIC AST type enumerant.
/// @return Equivalent semantic analyzer type.
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

/// @brief Resolve the textual name for a builtin call enumerant.
///
/// @param b Builtin identifier.
/// @return Null-terminated string describing the builtin.
const char *builtinName(BuiltinCallExpr::Builtin b)
{
    return getBuiltinInfo(b).name;
}

/// @brief Provide a printable name for a semantic type.
///
/// @details Used primarily for diagnostics so users receive BASIC-friendly type
///          names when reporting mismatches.
///
/// @param type Semantic analyzer type enumerant.
/// @return Human-readable name.
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

/// @brief Retrieve a diagnostic-friendly name for logical operators.
///
/// @param op Logical operator enumerant from the AST.
/// @return Canonical textual representation.
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

/// @brief Produce a printable snippet for conditional expressions.
///
/// @details Used when reporting diagnostics on conditional statements. Handles
///          the small subset of literal/identifier forms that appear in
///          short-circuit checks while returning an empty string for complex
///          expressions.
///
/// @param expr AST expression to convert to text.
/// @return BASIC-flavoured textual representation when possible.
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
