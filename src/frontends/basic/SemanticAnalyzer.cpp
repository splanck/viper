//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SemanticAnalyzer.cpp
// Purpose: Provide the constructor and shared helper routines for the BASIC
//          semantic analyzer, including lookup utilities used across the
//          statement- and procedure-specific translation units.
// Key invariants: Symbol tables mirror analyzer state; helper routines remain
//                 internal to the BASIC frontend; builtin registries must stay
//                 synchronised with the runtime definitions.
// Ownership/Lifetime: Borrowed DiagnosticEmitter; AST nodes and symbol tables
//                     are owned externally by the driver that orchestrates
//                     semantic analysis phases.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

/// @brief Construct a semantic analyzer wired to the provided diagnostic sink.
///
/// @details The analyzer owns a builtin registry whose diagnostics should flow
///          through the same @ref DiagnosticEmitter used by the parent driver.
///          Initialising the registry here guarantees consistent reporting from
///          all semantic checks.
///
/// @param emitter Diagnostic emitter used for user-facing messages.
SemanticAnalyzer::SemanticAnalyzer(DiagnosticEmitter &emitter)
    : de(emitter), procReg_(de)
{
}

/// @brief Access the set of symbols encountered during analysis.
///
/// @details The symbol set contains canonicalised names after scope resolution.
///          Callers use it to detect unused declarations or provide completion
///          suggestions.
///
/// @return Reference to the mutable symbol set owned by the analyzer.
const std::unordered_set<std::string> &SemanticAnalyzer::symbols() const
{
    return symbols_;
}

/// @brief Access the set of declared numeric labels.
///
/// @details BASIC statements may define numeric line labels used as jump
///          targets.  The analyzer records each definition to validate forward
///          references and to emit helpful diagnostics for duplicates.
///
/// @return Reference to the label definition set.
const std::unordered_set<int> &SemanticAnalyzer::labels() const
{
    return labels_;
}

/// @brief Access the set of referenced labels.
///
/// @details Tracking references separately from declarations enables the driver
///          to spot unresolved labels once analysis concludes.
///
/// @return Reference to the label reference set.
const std::unordered_set<int> &SemanticAnalyzer::labelRefs() const
{
    return labelRefs_;
}

/// @brief Retrieve the procedure registry maintained by the analyzer.
///
/// @details Procedures are managed by a dedicated registry that records
///          signatures and bodies.  The accessor exposes the immutable view used
///          during lowering.
///
/// @return Procedure table collected during semantic analysis.
const ProcTable &SemanticAnalyzer::procs() const
{
    return procReg_.procs();
}

/// @brief Query the inferred type for a variable if known.
///
/// @details The analyzer lazily assigns types based on declarations and naming
///          conventions.  When a variable has not been seen yet the function
///          returns @c std::nullopt so callers can fall back to defaults.
///
/// @param name Canonical variable identifier.
/// @return Optional semantic type for the variable.
std::optional<SemanticAnalyzer::Type>
SemanticAnalyzer::lookupVarType(const std::string &name) const
{
    if (auto it = varTypes_.find(name); it != varTypes_.end())
        return it->second;
    return std::nullopt;
}

/// @brief Resolve a symbol through the active scopes and update tracking tables.
///
/// @details Symbols first pass through the scope resolver, which maps aliases to
///          canonical names.  For definitions the helper inserts the symbol into
///          the tracked set, records mutations for procedure-local scopes, and
///          ensures type tables carry the proper default (based on suffix naming
///          conventions or forced defaults for INPUT targets).
///
/// @param name Symbol to resolve; updated in place to its canonical spelling.
/// @param kind Classification describing how the symbol is being used.
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

/// @brief Compute the Levenshtein distance between two identifiers.
///
/// @details Used to provide edit-distance-based suggestions in diagnostics when
///          a symbol lookup fails.  The implementation follows the classic
///          dynamic programming algorithm with two rolling buffers to minimise
///          allocations.
///
/// @param a First string to compare.
/// @param b Second string to compare.
/// @return Minimum number of single-character edits needed to transform @p a into @p b.
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

/// @brief Convert an AST type enumeration into a semantic analyzer type.
///
/// @details The semantic analyzer stores its own compact enum separate from the
///          AST representation.  This helper bridges the two domains so lookup
///          helpers share a consistent set of type tags.
///
/// @param ty AST type enumerator.
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

/// @brief Retrieve the canonical name for a builtin procedure.
///
/// @details Delegates to the builtin registry table, ensuring diagnostics and
///          code generation use the same naming scheme.
///
/// @param b Builtin enumerator.
/// @return Null-terminated name string.
const char *builtinName(BuiltinCallExpr::Builtin b)
{
    return getBuiltinInfo(b).name;
}

/// @brief Render a semantic type enumeration as human-readable text.
///
/// @details Used primarily by diagnostics when explaining inferred types or
///          mismatches to the user.
///
/// @param type Semantic analyzer type enumerator.
/// @return Uppercase string literal describing @p type.
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

/// @brief Retrieve the textual representation of a logical operator.
///
/// @details BASIC distinguishes between short-circuit and eager logical
///          operators.  The helper maps each opcode to the keyword emitted in
///          diagnostics.
///
/// @param op Logical operator enumerator.
/// @return Keyword string identifying the operator.
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

/// @brief Produce a textual approximation of a condition expression.
///
/// @details Diagnostic routines summarise conditions in error messages.  The
///          helper inspects common expression forms (variables and literals) and
///          renders a lightweight textual description suitable for insertion in
///          prose.  Expressions that are not recognised yield an empty string so
///          callers can fall back to generic wording.
///
/// @param expr Expression to describe.
/// @return Textual representation or empty string when not representable.
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
