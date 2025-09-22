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
// Links: docs/class-catalog.md

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <limits>
#include <sstream>

namespace il::frontends::basic
{

using semantic_analyzer_detail::levenshtein;
using semantic_analyzer_detail::logicalOpName;
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

    void visit(CallExpr &expr) override { result_ = analyzer_.analyzeCall(expr); }

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
        if (v.name.back() == '$')
            return Type::String;
        if (v.name.back() == '#')
            return Type::Float;
    }
    return Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeUnary(const UnaryExpr &u)
{
    Type t = Type::Unknown;
    if (u.expr)
        t = visitExpr(*u.expr);
    if (u.op == UnaryExpr::Op::LogicalNot)
    {
        if (t != Type::Unknown && t != Type::Bool)
        {
            std::ostringstream oss;
            oss << "NOT requires a BOOLEAN operand, got " << semanticTypeName(t) << '.';
            de.emit(il::support::Severity::Error,
                    std::string(DiagNonBooleanNotOperand),
                    u.loc,
                    3,
                    oss.str());
        }
        return Type::Bool;
    }
    return Type::Unknown;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeBinary(const BinaryExpr &b)
{
    Type lt = Type::Unknown;
    Type rt = Type::Unknown;
    if (b.lhs)
        lt = visitExpr(*b.lhs);
    if (b.rhs)
        rt = visitExpr(*b.rhs);
    switch (b.op)
    {
        case BinaryExpr::Op::Add:
        case BinaryExpr::Op::Sub:
        case BinaryExpr::Op::Mul:
            return analyzeArithmetic(b, lt, rt);
        case BinaryExpr::Op::Div:
        case BinaryExpr::Op::IDiv:
        case BinaryExpr::Op::Mod:
            return analyzeDivMod(b, lt, rt);
        case BinaryExpr::Op::Eq:
        case BinaryExpr::Op::Ne:
        case BinaryExpr::Op::Lt:
        case BinaryExpr::Op::Le:
        case BinaryExpr::Op::Gt:
        case BinaryExpr::Op::Ge:
            return analyzeComparison(b, lt, rt);
        case BinaryExpr::Op::LogicalAndShort:
        case BinaryExpr::Op::LogicalOrShort:
        case BinaryExpr::Op::LogicalAnd:
        case BinaryExpr::Op::LogicalOr:
            return analyzeLogical(b, lt, rt);
    }
    return Type::Unknown;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeArithmetic(const BinaryExpr &b,
                                                           Type lt,
                                                           Type rt)
{
    auto isNum = [](Type t) { return t == Type::Int || t == Type::Float || t == Type::Unknown; };
    if (!isNum(lt) || !isNum(rt))
    {
        std::string msg = "operand type mismatch";
        de.emit(il::support::Severity::Error, "B2001", b.loc, 1, std::move(msg));
    }
    return (lt == Type::Float || rt == Type::Float) ? Type::Float : Type::Int;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeDivMod(const BinaryExpr &b,
                                                       Type lt,
                                                       Type rt)
{
    auto isNum = [](Type t) { return t == Type::Int || t == Type::Float || t == Type::Unknown; };
    switch (b.op)
    {
        case BinaryExpr::Op::Div:
        {
            if (!isNum(lt) || !isNum(rt))
            {
                std::string msg = "operand type mismatch";
                de.emit(il::support::Severity::Error, "B2001", b.loc, 1, std::move(msg));
            }
            if (lt == Type::Float || rt == Type::Float)
                return Type::Float;
            if (dynamic_cast<const IntExpr *>(b.lhs.get()) &&
                dynamic_cast<const IntExpr *>(b.rhs.get()))
            {
                auto *ri = static_cast<const IntExpr *>(b.rhs.get());
                if (ri->value == 0)
                {
                    std::string msg = "divide by zero";
                    de.emit(il::support::Severity::Error, "B2002", b.loc, 1, std::move(msg));
                }
            }
            return Type::Int;
        }
        case BinaryExpr::Op::IDiv:
        case BinaryExpr::Op::Mod:
        {
            if ((lt != Type::Unknown && lt != Type::Int) ||
                (rt != Type::Unknown && rt != Type::Int))
            {
                std::string msg = "operand type mismatch";
                de.emit(il::support::Severity::Error, "B2001", b.loc, 1, std::move(msg));
            }
            if (dynamic_cast<const IntExpr *>(b.lhs.get()) &&
                dynamic_cast<const IntExpr *>(b.rhs.get()))
            {
                auto *ri = static_cast<const IntExpr *>(b.rhs.get());
                if (ri->value == 0)
                {
                    std::string msg = "divide by zero";
                    de.emit(il::support::Severity::Error, "B2002", b.loc, 1, std::move(msg));
                }
            }
            return Type::Int;
        }
        default:
            break;
    }
    return Type::Unknown;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeComparison(const BinaryExpr &b,
                                                           Type lt,
                                                           Type rt)
{
    auto isNum = [](Type t) { return t == Type::Int || t == Type::Float || t == Type::Unknown; };
    auto isStr = [](Type t) { return t == Type::String || t == Type::Unknown; };

    const bool numeric_ok = isNum(lt) && isNum(rt);
    const bool string_ok =
        isStr(lt) && isStr(rt) && (b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne);

    if (string_ok)
        return Type::Bool;

    if (!numeric_ok)
    {
        std::string msg = "operand type mismatch";
        de.emit(il::support::Severity::Error, "B2001", b.loc, 1, std::move(msg));
        return Type::Bool;
    }

    return Type::Bool;
}

SemanticAnalyzer::Type SemanticAnalyzer::analyzeLogical(const BinaryExpr &b,
                                                        Type lt,
                                                        Type rt)
{
    auto isBool = [](Type t) { return t == Type::Unknown || t == Type::Bool; };
    if (!isBool(lt) || !isBool(rt))
    {
        std::ostringstream oss;
        oss << "Logical operator " << logicalOpName(b.op)
            << " requires BOOLEAN operands, got " << semanticTypeName(lt) << " and "
            << semanticTypeName(rt) << '.';
        de.emit(il::support::Severity::Error,
                std::string(DiagNonBooleanLogicalOperand),
                b.loc,
                1,
                oss.str());
    }
    return Type::Bool;
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

SemanticAnalyzer::Type SemanticAnalyzer::visitExpr(Expr &e)
{
    SemanticAnalyzerExprVisitor visitor(*this);
    e.accept(visitor);
    return visitor.result();
}

} // namespace il::frontends::basic
