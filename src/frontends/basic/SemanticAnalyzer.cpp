// File: src/frontends/basic/SemanticAnalyzer.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Houses shared helpers and common utilities for the BASIC semantic
//          analyzer along with constructor and accessors.
// Key invariants: Symbol tables mirror analyzer state; helper routines remain
//                 internal to the BASIC frontend.
// Ownership/Lifetime: Borrowed DiagnosticEmitter; AST nodes owned externally.
// Links: docs/codemap.md

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic
{

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEmitter &emitter)
    : de(emitter), procReg_(de)
{
}

const std::unordered_set<std::string> &SemanticAnalyzer::symbols() const
{
    return symbols_;
}

const std::unordered_set<int> &SemanticAnalyzer::labels() const
{
    return labels_;
}

const std::unordered_set<int> &SemanticAnalyzer::labelRefs() const
{
    return labelRefs_;
}

const ProcTable &SemanticAnalyzer::procs() const
{
    return procReg_.procs();
}

std::optional<SemanticAnalyzer::Type>
SemanticAnalyzer::lookupVarType(const std::string &name) const
{
    if (auto it = varTypes_.find(name); it != varTypes_.end())
        return it->second;
    return std::nullopt;
}

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

const char *builtinName(BuiltinCallExpr::Builtin b)
{
    return getBuiltinInfo(b).name;
}

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
