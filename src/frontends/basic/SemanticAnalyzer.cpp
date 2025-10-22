//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides shared utilities for the BASIC semantic analyzer, including the
// constructor, accessor methods, and general-purpose helpers.  Keeping these
// routines out-of-line centralises symbol tracking behaviour, default type
// resolution, and Levenshtein distance computation while leaving the header
// focused on the public interface.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

/// @brief Construct the semantic analyzer with its diagnostic sink.
///
/// @details Stores the supplied diagnostic emitter and initialises the
///          procedure registry so procedure diagnostics reuse the same sink.
///
/// @param emitter Diagnostic engine used to report errors.
SemanticAnalyzer::SemanticAnalyzer(DiagnosticEmitter &emitter)
    : de(emitter), procReg_(de)
{
}

/// @brief Access the set of symbols referenced during analysis.
///
/// @return Immutable view of tracked symbol names.
const std::unordered_set<std::string> &SemanticAnalyzer::symbols() const
{
    return symbols_;
}

/// @brief Access the set of labels defined within the current scope.
///
/// @return Immutable view of labels recorded so far.
const std::unordered_set<int> &SemanticAnalyzer::labels() const
{
    return labels_;
}

/// @brief Access the set of labels referenced by jump statements.
///
/// @return Immutable view of label references.
const std::unordered_set<int> &SemanticAnalyzer::labelRefs() const
{
    return labelRefs_;
}

/// @brief Expose the procedure registry maintained by the analyzer.
///
/// @return Reference to the internal procedure table.
const ProcTable &SemanticAnalyzer::procs() const
{
    return procReg_.procs();
}

/// @brief Look up the semantic type previously assigned to a variable.
///
/// @param name Variable name, after any scope resolution.
/// @return Optional semantic type when present in the map.
std::optional<SemanticAnalyzer::Type>
SemanticAnalyzer::lookupVarType(const std::string &name) const
{
    if (auto it = varTypes_.find(name); it != varTypes_.end())
        return it->second;
    return std::nullopt;
}

/// @brief Resolve a symbol through scopes and ensure bookkeeping is updated.
///
/// @details Aliases introduced by `DEF` statements are applied first.  New
///          definitions are inserted into the symbol set and, when inside a
///          procedure, recorded for scope unwinding.  Symbols receive default
///          types based on BASIC suffix conventions or statement context so the
///          analyzer can reason about future references without additional
///          lookups.
///
/// @param name Symbol name; updated in place when alias resolution occurs.
/// @param kind Indicates whether the symbol is being defined or merely referenced.
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
/// @details Uses a rolling two-row dynamic-programming table to keep the
///          implementation efficient while producing accurate edit distances for
///          diagnostic suggestions.
///
/// @param a First string to compare.
/// @param b Second string to compare.
/// @return Minimum edit distance between the two strings.
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

/// @brief Translate a BASIC AST type into the semantic analyzer's type enum.
///
/// @param ty AST type enumerator produced by the parser.
/// @return Corresponding semantic analyzer type.
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

/// @brief Retrieve the canonical name associated with a builtin call.
///
/// @param b Builtin identifier.
/// @return Null-terminated string naming the builtin.
const char *builtinName(BuiltinCallExpr::Builtin b)
{
    return getBuiltinInfo(b).name;
}

/// @brief Map a semantic type to the diagnostic name used in messages.
///
/// @param type Semantic analyzer type classification.
/// @return Null-terminated diagnostic string.
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
