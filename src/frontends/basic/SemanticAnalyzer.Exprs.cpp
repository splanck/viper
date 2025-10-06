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

#include <array>
#include <limits>
#include <sstream>

namespace il::frontends::basic::semantic_analyzer_detail
{

namespace
{
constexpr std::size_t exprRuleCount() noexcept
{
    return static_cast<std::size_t>(BinaryExpr::Op::LogicalOr) + 1;
}
} // namespace

constexpr bool isNumericType(SemanticAnalyzer::Type type) noexcept
{
    using Type = SemanticAnalyzer::Type;
    return type == Type::Int || type == Type::Float || type == Type::Unknown;
}

constexpr bool isIntegerType(SemanticAnalyzer::Type type) noexcept
{
    using Type = SemanticAnalyzer::Type;
    return type == Type::Int || type == Type::Unknown;
}

constexpr bool isBooleanType(SemanticAnalyzer::Type type) noexcept
{
    using Type = SemanticAnalyzer::Type;
    return type == Type::Bool || type == Type::Unknown;
}

constexpr bool isStringType(SemanticAnalyzer::Type type) noexcept
{
    using Type = SemanticAnalyzer::Type;
    return type == Type::String || type == Type::Unknown;
}

SemanticAnalyzer::Type numericResult(SemanticAnalyzer::Type lhs,
                                     SemanticAnalyzer::Type rhs) noexcept
{
    using Type = SemanticAnalyzer::Type;
    return (lhs == Type::Float || rhs == Type::Float) ? Type::Float : Type::Int;
}

static SemanticAnalyzer::Type addResult(SemanticAnalyzer::Type lhs,
                                        SemanticAnalyzer::Type rhs) noexcept
{
    using T = SemanticAnalyzer::Type;
    if (lhs == T::String && rhs == T::String) return T::String;
    return (lhs == T::Float || rhs == T::Float) ? T::Float : T::Int;
}

SemanticAnalyzer::Type powResult(SemanticAnalyzer::Type,
                                 SemanticAnalyzer::Type) noexcept
{
    return SemanticAnalyzer::Type::Float;
}

SemanticAnalyzer::Type integerResult(SemanticAnalyzer::Type, SemanticAnalyzer::Type) noexcept
{
    return SemanticAnalyzer::Type::Int;
}

SemanticAnalyzer::Type booleanResult(SemanticAnalyzer::Type, SemanticAnalyzer::Type) noexcept
{
    return SemanticAnalyzer::Type::Bool;
}

std::string formatLogicalOperandMessage(BinaryExpr::Op op,
                                        SemanticAnalyzer::Type lhs,
                                        SemanticAnalyzer::Type rhs)
{
    std::ostringstream oss;
    oss << "Logical operator " << logicalOpName(op) << " requires BOOLEAN operands, got "
        << semanticTypeName(lhs) << " and " << semanticTypeName(rhs) << '.';
    return oss.str();
}

const ExprRule &exprRule(BinaryExpr::Op op)
{
    static const std::array<ExprRule, exprRuleCount()> rules = {
        {{BinaryExpr::Op::Add,
          &SemanticAnalyzer::validateNumericOperands,
          &addResult,
          "B2001"},
         {BinaryExpr::Op::Sub,
          &SemanticAnalyzer::validateNumericOperands,
          &numericResult,
          "B2001"},
         {BinaryExpr::Op::Mul,
          &SemanticAnalyzer::validateNumericOperands,
          &numericResult,
          "B2001"},
         {BinaryExpr::Op::Div,
          &SemanticAnalyzer::validateDivisionOperands,
          &numericResult,
          "B2001"},
         {BinaryExpr::Op::Pow,
          &SemanticAnalyzer::validateNumericOperands,
          &powResult,
          "B2001"},
         {BinaryExpr::Op::IDiv,
          &SemanticAnalyzer::validateIntegerOperands,
          &integerResult,
          "B2001"},
         {BinaryExpr::Op::Mod,
          &SemanticAnalyzer::validateIntegerOperands,
          &integerResult,
          "B2001"},
         {BinaryExpr::Op::Eq,
          &SemanticAnalyzer::validateComparisonOperands,
          &booleanResult,
          "B2001"},
         {BinaryExpr::Op::Ne,
          &SemanticAnalyzer::validateComparisonOperands,
          &booleanResult,
          "B2001"},
         {BinaryExpr::Op::Lt,
          &SemanticAnalyzer::validateComparisonOperands,
          &booleanResult,
          "B2001"},
         {BinaryExpr::Op::Le,
          &SemanticAnalyzer::validateComparisonOperands,
          &booleanResult,
          "B2001"},
         {BinaryExpr::Op::Gt,
          &SemanticAnalyzer::validateComparisonOperands,
          &booleanResult,
          "B2001"},
         {BinaryExpr::Op::Ge,
          &SemanticAnalyzer::validateComparisonOperands,
          &booleanResult,
          "B2001"},
         {BinaryExpr::Op::LogicalAndShort,
          &SemanticAnalyzer::validateLogicalOperands,
          &booleanResult,
          SemanticAnalyzer::DiagNonBooleanLogicalOperand},
         {BinaryExpr::Op::LogicalOrShort,
          &SemanticAnalyzer::validateLogicalOperands,
          &booleanResult,
          SemanticAnalyzer::DiagNonBooleanLogicalOperand},
         {BinaryExpr::Op::LogicalAnd,
          &SemanticAnalyzer::validateLogicalOperands,
          &booleanResult,
          SemanticAnalyzer::DiagNonBooleanLogicalOperand},
         {BinaryExpr::Op::LogicalOr,
          &SemanticAnalyzer::validateLogicalOperands,
          &booleanResult,
          SemanticAnalyzer::DiagNonBooleanLogicalOperand}}};
    return rules.at(static_cast<std::size_t>(op));
}

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic
{

using semantic_analyzer_detail::exprRule;
using semantic_analyzer_detail::levenshtein;
using semantic_analyzer_detail::semanticTypeName;

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
    const auto &rule = exprRule(b.op);
    if (rule.validator)
        (this->*(rule.validator))(b, lt, rt, rule.mismatchDiag);
    if (rule.result)
        return rule.result(lt, rt);
    return Type::Unknown;
}

void SemanticAnalyzer::emitOperandTypeMismatch(const BinaryExpr &expr, std::string_view diagId)
{
    if (diagId.empty())
        return;

    std::string msg = "operand type mismatch";
    de.emit(il::support::Severity::Error,
            std::string(diagId),
            expr.loc,
            1,
            std::move(msg));
}

void SemanticAnalyzer::emitDivideByZero(const BinaryExpr &expr)
{
    std::string msg = "divide by zero";
    de.emit(il::support::Severity::Error, "B2002", expr.loc, 1, std::move(msg));
}

bool SemanticAnalyzer::rhsIsLiteralZero(const BinaryExpr &expr) const
{
    const auto *ri = dynamic_cast<const IntExpr *>(expr.rhs.get());
    return ri != nullptr && ri->value == 0;
}

void SemanticAnalyzer::validateNumericOperands(const BinaryExpr &expr,
                                               Type lhs,
                                               Type rhs,
                                               std::string_view diagId)
{
    if (!semantic_analyzer_detail::isNumericType(lhs) ||
        !semantic_analyzer_detail::isNumericType(rhs))
    {
        emitOperandTypeMismatch(expr, diagId);
    }
}

void SemanticAnalyzer::validateDivisionOperands(const BinaryExpr &expr,
                                                Type lhs,
                                                Type rhs,
                                                std::string_view diagId)
{
    validateNumericOperands(expr, lhs, rhs, diagId);
    if (dynamic_cast<const IntExpr *>(expr.lhs.get()) && rhsIsLiteralZero(expr))
    {
        emitDivideByZero(expr);
    }
}

void SemanticAnalyzer::validateIntegerOperands(const BinaryExpr &expr,
                                               Type lhs,
                                               Type rhs,
                                               std::string_view diagId)
{
    if (!semantic_analyzer_detail::isIntegerType(lhs) ||
        !semantic_analyzer_detail::isIntegerType(rhs))
    {
        emitOperandTypeMismatch(expr, diagId);
    }
    if (dynamic_cast<const IntExpr *>(expr.lhs.get()) && rhsIsLiteralZero(expr))
    {
        emitDivideByZero(expr);
    }
}

void SemanticAnalyzer::validateComparisonOperands(const BinaryExpr &expr,
                                                  Type lhs,
                                                  Type rhs,
                                                  std::string_view diagId)
{
    const bool allowStrings =
        expr.op == BinaryExpr::Op::Eq || expr.op == BinaryExpr::Op::Ne;
    const bool numericOk = semantic_analyzer_detail::isNumericType(lhs) &&
                           semantic_analyzer_detail::isNumericType(rhs);
    const bool stringOk = allowStrings && semantic_analyzer_detail::isStringType(lhs) &&
                          semantic_analyzer_detail::isStringType(rhs);

    if (!numericOk && !stringOk)
        emitOperandTypeMismatch(expr, diagId);
}

void SemanticAnalyzer::validateLogicalOperands(const BinaryExpr &expr,
                                               Type lhs,
                                               Type rhs,
                                               std::string_view diagId)
{
    if (semantic_analyzer_detail::isBooleanType(lhs) &&
        semantic_analyzer_detail::isBooleanType(rhs))
        return;

    de.emit(il::support::Severity::Error,
            std::string(diagId),
            expr.loc,
            1,
            semantic_analyzer_detail::formatLogicalOperandMessage(expr.op, lhs, rhs));
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
