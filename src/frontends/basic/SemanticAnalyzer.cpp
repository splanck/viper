//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Houses shared helpers and common utilities for the BASIC semantic analyzer
// along with constructor and accessors.  The routines centralise bookkeeping of
// symbol tables and string formatting utilities used by error reporting.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Shared infrastructure for the BASIC semantic analyzer.
/// @details Provides lightweight accessors and helper algorithms that are used
///          across the analyzer's translation units.  Keeping these definitions
///          here reduces header churn and avoids scattering utility functions
///          across unrelated files.

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

/// @brief Construct the analyzer and initialise dependent registries.
///
/// @details Stores a reference to the diagnostic emitter for later use and
///          initialises the procedure registry with the same emitter so that
///          diagnostics share formatting helpers.
SemanticAnalyzer::SemanticAnalyzer(DiagnosticEmitter &emitter)
    : de(emitter), procReg_(de)
{
}

/// @brief Access the set of symbols observed during semantic analysis.
///
/// @details The set contains canonicalised symbol names after scope rewriting.
///          Callers use it for diagnostics such as duplicate symbol detection.
const std::unordered_set<std::string> &SemanticAnalyzer::symbols() const
{
    return symbols_;
}

/// @brief Access the set of declared numeric labels.
///
/// @details Labels are recorded to validate `GOTO`/`GOSUB` targets.  The set
///          contains integer identifiers exactly as parsed from the source.
const std::unordered_set<int> &SemanticAnalyzer::labels() const
{
    return labels_;
}

/// @brief Access the set of referenced labels.
///
/// @details Used to spot missing labels by comparing against `labels()`.
const std::unordered_set<int> &SemanticAnalyzer::labelRefs() const
{
    return labelRefs_;
}

/// @brief Retrieve the current procedure table snapshot.
///
/// @details Exposes the registry maintained by the analyzer, allowing callers
///          to inspect declared procedures after semantic passes complete.
const ProcTable &SemanticAnalyzer::procs() const
{
    return procReg_.procs();
}

/// @brief Look up the semantic type associated with a variable name.
///
/// @details Returns the type recorded in `varTypes_` if present.  When the
///          variable has not been seen yet, `std::nullopt` indicates that the
///          caller should apply default typing rules.
std::optional<SemanticAnalyzer::Type>
SemanticAnalyzer::lookupVarType(const std::string &name) const
{
    if (auto it = varTypes_.find(name); it != varTypes_.end())
        return it->second;
    return std::nullopt;
}

/// @brief Canonicalise a symbol name and record it in analyzer state.
///
/// @details Resolves the symbol through the active scope, inserts it into the
///          global symbol set when appropriate, applies default typing based on
///          the BASIC suffix rules, and notifies the active procedure scope of
///          any mutations so undo operations remain possible.
///
/// @param name Symbol identifier to normalise; updated in place when scope
///             resolution remaps it.
/// @param kind Classifies how the symbol is used (definition, reference, etc.).
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
/// @details Allocates two scratch buffers sized to the second string and fills
///          them iteratively, keeping the algorithm O(mn) but with linear
///          auxiliary storage.  The routine is used to rank symbol suggestions
///          in diagnostics.
///
/// @param a First string operand.
/// @param b Second string operand.
/// @return Number of single-character edits required to transform @p a into @p b.
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

/// @brief Map an AST-level type enumeration to the semantic enum.
///
/// @details Bridges the frontend's parse-time enumeration to the analyzer's
///          representation so that downstream code can work purely with
///          `SemanticAnalyzer::Type`.
///
/// @param ty AST type enumerator.
/// @return Equivalent semantic type classification.
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

/// @brief Retrieve the canonical name for a builtin call.
///
/// @details Delegates to the builtin registry metadata and exposes the `name`
///          field directly for diagnostics.
///
/// @param b Builtin enumerator to describe.
/// @return Null-terminated string containing the builtin name.
const char *builtinName(BuiltinCallExpr::Builtin b)
{
    return getBuiltinInfo(b).name;
}

/// @brief Convert a semantic type into its human-readable display form.
///
/// @details Provides the text used in diagnostics to describe inferred or
///          expected types.  Array types currently use fixed labels.
///
/// @param type Semantic type to stringify.
/// @return Stable string literal describing the type.
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

/// @brief Pretty-print a logical operator enumerator.
///
/// @details Returns BASIC-specific spellings for the logical operators so error
///          messages can reference the source syntax directly.
///
/// @param op Logical operator enumerator.
/// @return String literal containing the operator name or a fallback marker.
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

/// @brief Produce a textual representation of a condition expression.
///
/// @details Attempts to recover the original source tokens for simple literals
///          and variable references so diagnostics can echo user-written
///          expressions.  Complex expressions fall back to an empty string.
///
/// @param expr Expression to serialise.
/// @return String approximating the original source text when recognised.
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
