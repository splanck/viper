// File: src/frontends/basic/SemanticAnalyzer.Exprs.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements expression analysis for the BASIC semantic analyzer,
//          including variable resolution, operator checking, and array access
//          validation.
// Key invariants: Expression analysis reports type mismatches and symbol
//                 resolution issues; visitors defer to SemanticAnalyzer
//                 helpers.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; AST nodes owned
//                     externally.
// Links: docs/codemap.md

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <limits>

namespace il::frontends::basic::semantic_analyzer_detail
{

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic
{

using semantic_analyzer_detail::levenshtein;
using semantic_analyzer_detail::semanticTypeName;

class SemanticAnalyzerExprVisitor final : public MutExprVisitor
{
  public:
    explicit SemanticAnalyzerExprVisitor(SemanticAnalyzer &analyzer) noexcept
        : analyzer_(analyzer)
    {
    }

    void visit(IntExpr &) override { result_ = SemanticAnalyzer::Type::Int; }
    void visit(FloatExpr &) override { result_ = SemanticAnalyzer::Type::Float; }
    void visit(StringExpr &) override { result_ = SemanticAnalyzer::Type::String; }
    void visit(BoolExpr &) override { result_ = SemanticAnalyzer::Type::Bool; }

    void visit(VarExpr &expr) override { result_ = analyzer_.analyzeVar(expr); }

    void visit(ArrayExpr &expr) override { result_ = analyzer_.analyzeArray(expr); }

    void visit(UnaryExpr &expr) override { result_ = analyzer_.analyzeUnary(expr); }

    void visit(BinaryExpr &expr) override { result_ = analyzer_.analyzeBinary(expr); }

    void visit(BuiltinCallExpr &expr) override { result_ = analyzer_.analyzeBuiltinCall(expr); }

    void visit(LBoundExpr &expr) override { result_ = analyzer_.analyzeLBound(expr); }

    void visit(UBoundExpr &expr) override { result_ = analyzer_.analyzeUBound(expr); }

    void visit(CallExpr &expr) override { result_ = analyzer_.analyzeCall(expr); }

    void visit(NewExpr &) override { result_ = SemanticAnalyzer::Type::Unknown; }
    void visit(MeExpr &) override { result_ = SemanticAnalyzer::Type::Unknown; }
    void visit(MemberAccessExpr &) override { result_ = SemanticAnalyzer::Type::Unknown; }
    void visit(MethodCallExpr &) override { result_ = SemanticAnalyzer::Type::Unknown; }

    [[nodiscard]] SemanticAnalyzer::Type result() const noexcept { return result_; }

  private:
    SemanticAnalyzer &analyzer_;
    SemanticAnalyzer::Type result_{SemanticAnalyzer::Type::Unknown};
};

SemanticAnalyzer::Type SemanticAnalyzer::analyzeVar(VarExpr &v)
{
    resolveAndTrackSymbol(v.name, SymbolKind::Reference);
    if (!symbols_.count(v.name))
    {
        std::string best;
        size_t bestDist = std::numeric_limits<size_t>::max();
        for (const auto &s : symbols_)
        {
            size_t d = levenshtein(v.name, s);
            if (d < bestDist)
            {
                bestDist = d;
                best = s;
            }
        }
        std::string msg = "unknown variable '" + v.name + "'";
        if (!best.empty())
            msg += "; did you mean '" + best + "'?";
        de.emit(il::support::Severity::Error,
                "B1001",
                v.loc,
                static_cast<uint32_t>(v.name.size()),
                std::move(msg));
        return Type::Unknown;
    }
    auto it = varTypes_.find(v.name);
    if (it != varTypes_.end())
        return it->second;
    if (!v.name.empty())
    {
        // BASIC suffix rules provide implicit types for undeclared variables:
        // '$' for STRING and '#'/'!' for floating point. Tracking this here keeps
        // later passes aligned with language defaults even when declarations are
        // omitted in source.
        if (v.name.back() == '$')
            return Type::String;
        if (v.name.back() == '#' || v.name.back() == '!')
            return Type::Float;
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeUnary(const UnaryExpr &u)
{
    return sem::analyzeUnaryExpr(*this, u);
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeBinary(const BinaryExpr &b)
{
    return sem::analyzeBinaryExpr(*this, b);
}

void SemanticAnalyzer::markImplicitConversion(const Expr &expr, Type targetType)
{
    implicitConversions_[&expr] = targetType;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeArray(ArrayExpr &a)
{
    resolveAndTrackSymbol(a.name, SymbolKind::Reference);
    if (!arrays_.count(a.name))
    {
        std::string msg = "unknown array '" + a.name + "'";
        de.emit(il::support::Severity::Error,
                "B1001",
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::move(msg));
        visitExpr(*a.index);
        return Type::Unknown;
    }
    if (auto itType = varTypes_.find(a.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        std::string msg = "variable '" + a.name + "' is not an array";
        de.emit(il::support::Severity::Error,
                "B2001",
                a.loc,
                static_cast<uint32_t>(a.name.size()),
                std::move(msg));
        visitExpr(*a.index);
        return Type::Unknown;
    }
    Type ty = visitExpr(*a.index);
    if (ty != Type::Unknown && ty != Type::Int)
    {
        std::string msg = "index type mismatch";
        de.emit(il::support::Severity::Error, "B2001", a.loc, 1, std::move(msg));
    }
    auto it = arrays_.find(a.name);
    if (it != arrays_.end() && it->second >= 0)
    {
        if (auto *ci = dynamic_cast<const IntExpr *>(a.index.get()))
        {
            if (ci->value < 0 || ci->value >= it->second)
            {
                std::string msg = "index out of bounds";
                de.emit(il::support::Severity::Warning, "B3001", a.loc, 1, std::move(msg));
            }
        }
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLBound(LBoundExpr &expr)
{
    resolveAndTrackSymbol(expr.name, SymbolKind::Reference);
    if (!arrays_.count(expr.name))
    {
        std::string msg = "unknown array '" + expr.name + "'";
        de.emit(il::support::Severity::Error,
                "B1001",
                expr.loc,
                static_cast<uint32_t>(expr.name.size()),
                std::move(msg));
        return Type::Unknown;
    }
    if (auto itType = varTypes_.find(expr.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        std::string msg = "variable '" + expr.name + "' is not an array";
        de.emit(il::support::Severity::Error,
                "B2001",
                expr.loc,
                static_cast<uint32_t>(expr.name.size()),
                std::move(msg));
        return Type::Unknown;
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeUBound(UBoundExpr &expr)
{
    resolveAndTrackSymbol(expr.name, SymbolKind::Reference);
    if (!arrays_.count(expr.name))
    {
        std::string msg = "unknown array '" + expr.name + "'";
        de.emit(il::support::Severity::Error,
                "B1001",
                expr.loc,
                static_cast<uint32_t>(expr.name.size()),
                std::move(msg));
        return Type::Unknown;
    }
    if (auto itType = varTypes_.find(expr.name);
        itType != varTypes_.end() && itType->second != Type::ArrayInt)
    {
        std::string msg = "variable '" + expr.name + "' is not an array";
        de.emit(il::support::Severity::Error,
                "B2001",
                expr.loc,
                static_cast<uint32_t>(expr.name.size()),
                std::move(msg));
        return Type::Unknown;
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::visitExpr(Expr &e)
{
    SemanticAnalyzerExprVisitor visitor(*this);
    e.accept(visitor);
    return visitor.result();
}

} // namespace il::frontends::basic
