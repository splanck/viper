//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic analysis helpers for binary expressions.
/// @details Provides operand validation, result inference, and implicit
///          conversion tracking for BASIC binary operators while unifying
///          diagnostic emission through Check_Common utilities.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/Check_Common.hpp"

#include <array>
#include <sstream>

namespace il::frontends::basic::semantic_analyzer_detail
{
using Type = SemanticAnalyzer::Type;

constexpr std::size_t exprRuleCount() noexcept
{
    return static_cast<std::size_t>(BinaryExpr::Op::LogicalOr) + 1;
}

constexpr bool isNumericType(Type type) noexcept
{
    return type == Type::Int || type == Type::Float || type == Type::Unknown;
}

constexpr bool isIntegerType(Type type) noexcept
{
    return type == Type::Int || type == Type::Unknown;
}

constexpr bool isBooleanType(Type type) noexcept
{
    return type == Type::Bool || type == Type::Unknown;
}

constexpr bool isStringType(Type type) noexcept
{
    return type == Type::String;
}

Type numericResult(Type lhs, Type rhs) noexcept
{
    return commonNumericType(lhs, rhs);
}

Type divisionResult(Type lhs, Type rhs) noexcept
{
    if (!isNumericType(lhs) || !isNumericType(rhs))
        return Type::Unknown;
    return Type::Float;
}

Type addResult(Type lhs, Type rhs) noexcept
{
    if (lhs == Type::String && rhs == Type::String)
        return Type::String;
    return commonNumericType(lhs, rhs);
}

Type powResult(Type, Type) noexcept
{
    return Type::Float;
}

Type integerResult(Type, Type) noexcept
{
    return Type::Int;
}

Type booleanResult(Type, Type) noexcept
{
    return Type::Bool;
}

bool rhsIsLiteralZero(const BinaryExpr &expr)
{
    if (const auto *ri = dynamic_cast<const IntExpr *>(expr.rhs.get()); ri != nullptr)
        return ri->value == 0;
    if (const auto *rf = dynamic_cast<const FloatExpr *>(expr.rhs.get()); rf != nullptr)
        return rf->value == 0.0;
    return false;
}

void validateNumericOperands(sem::ExprCheckContext &context,
                             const BinaryExpr &expr,
                             Type lhs,
                             Type rhs,
                             std::string_view diagId)
{
    if (!isNumericType(lhs) || !isNumericType(rhs))
        sem::emitOperandTypeMismatch(context.diagnostics(), expr, diagId);
}

void validateAddOperands(sem::ExprCheckContext &context,
                         const BinaryExpr &expr,
                         Type lhs,
                         Type rhs,
                         std::string_view diagId)
{
    const bool numericOk = isNumericType(lhs) && isNumericType(rhs);
    const bool stringOk = isStringType(lhs) && isStringType(rhs);
    if (!numericOk && !stringOk)
        sem::emitOperandTypeMismatch(context.diagnostics(), expr, diagId);
}

void validateDivisionOperands(sem::ExprCheckContext &context,
                              const BinaryExpr &expr,
                              Type lhs,
                              Type rhs,
                              std::string_view diagId)
{
    validateNumericOperands(context, expr, lhs, rhs, diagId);
    if (rhsIsLiteralZero(expr))
        sem::emitDivideByZero(context.diagnostics(), expr);
}

void validateIntegerOperands(sem::ExprCheckContext &context,
                             const BinaryExpr &expr,
                             Type lhs,
                             Type rhs,
                             std::string_view diagId)
{
    if (!isIntegerType(lhs) || !isIntegerType(rhs))
        sem::emitOperandTypeMismatch(context.diagnostics(), expr, diagId);
    if (rhsIsLiteralZero(expr))
        sem::emitDivideByZero(context.diagnostics(), expr);
}

void validateComparisonOperands(sem::ExprCheckContext &context,
                                const BinaryExpr &expr,
                                Type lhs,
                                Type rhs,
                                std::string_view diagId)
{
    const bool allowStrings =
        expr.op == BinaryExpr::Op::Eq || expr.op == BinaryExpr::Op::Ne;
    const bool numericOk = isNumericType(lhs) && isNumericType(rhs);
    const bool stringOk = allowStrings && isStringType(lhs) && isStringType(rhs);
    if (!numericOk && !stringOk)
        sem::emitOperandTypeMismatch(context.diagnostics(), expr, diagId);
}

void validateLogicalOperands(sem::ExprCheckContext &context,
                             const BinaryExpr &expr,
                             Type lhs,
                             Type rhs,
                             std::string_view diagId)
{
    if (isBooleanType(lhs) && isBooleanType(rhs))
        return;

    sem::emitTypeMismatch(context.diagnostics(),
                          std::string(diagId),
                          expr.loc,
                          1,
                          formatLogicalOperandMessage(expr.op, lhs, rhs));
}

SemanticAnalyzer::Type commonNumericType(Type lhs, Type rhs) noexcept
{
    return (lhs == Type::Float || rhs == Type::Float) ? Type::Float : Type::Int;
}

std::string formatLogicalOperandMessage(BinaryExpr::Op op, Type lhs, Type rhs)
{
    std::ostringstream oss;
    oss << "Logical operator " << logicalOpName(op) << " requires BOOLEAN operands, got "
        << semanticTypeName(lhs) << " and " << semanticTypeName(rhs) << '.';
    return oss.str();
}

const ExprRule &exprRule(BinaryExpr::Op op)
{
    static const std::array<ExprRule, exprRuleCount()> rules = {{
        {BinaryExpr::Op::Add, &validateAddOperands, &addResult, "B2001"},
        {BinaryExpr::Op::Sub, &validateNumericOperands, &numericResult, "B2001"},
        {BinaryExpr::Op::Mul, &validateNumericOperands, &numericResult, "B2001"},
        {BinaryExpr::Op::Div, &validateDivisionOperands, &divisionResult, "B2001"},
        {BinaryExpr::Op::Pow, &validateNumericOperands, &powResult, "B2001"},
        {BinaryExpr::Op::IDiv, &validateIntegerOperands, &integerResult, "B2001"},
        {BinaryExpr::Op::Mod, &validateIntegerOperands, &integerResult, "B2001"},
        {BinaryExpr::Op::Eq, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Ne, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Lt, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Le, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Gt, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::Ge, &validateComparisonOperands, &booleanResult, "B2001"},
        {BinaryExpr::Op::LogicalAndShort,
         &validateLogicalOperands,
         &booleanResult,
         SemanticAnalyzer::DiagNonBooleanLogicalOperand},
        {BinaryExpr::Op::LogicalOrShort,
         &validateLogicalOperands,
         &booleanResult,
         SemanticAnalyzer::DiagNonBooleanLogicalOperand},
        {BinaryExpr::Op::LogicalAnd,
         &validateLogicalOperands,
         &booleanResult,
         SemanticAnalyzer::DiagNonBooleanLogicalOperand},
        {BinaryExpr::Op::LogicalOr,
         &validateLogicalOperands,
         &booleanResult,
         SemanticAnalyzer::DiagNonBooleanLogicalOperand},
    }};

    return rules.at(static_cast<std::size_t>(op));
}

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic::sem
{

SemanticAnalyzer::Type analyzeBinaryExpr(SemanticAnalyzer &analyzer, const BinaryExpr &expr)
{
    ExprCheckContext context(analyzer);
    using Type = SemanticAnalyzer::Type;

    Type lhs = Type::Unknown;
    Type rhs = Type::Unknown;
    if (expr.lhs)
        lhs = context.evaluate(*expr.lhs);
    if (expr.rhs)
        rhs = context.evaluate(*expr.rhs);

    if (expr.op == BinaryExpr::Op::Add || expr.op == BinaryExpr::Op::Sub ||
        expr.op == BinaryExpr::Op::Mul)
    {
        if (semantic_analyzer_detail::isNumericType(lhs) &&
            semantic_analyzer_detail::isNumericType(rhs))
        {
            const Type target = semantic_analyzer_detail::commonNumericType(lhs, rhs);
            if (target == Type::Float)
            {
                // Promote INT operands to FLOAT when mixing numeric types and
                // record the implicit conversion so later passes can insert the
                // cast explicitly during lowering/codegen.
                if (expr.lhs && lhs != Type::Float && lhs != Type::Unknown)
                    context.markImplicitConversion(*expr.lhs, target);
                if (expr.rhs && rhs != Type::Float && rhs != Type::Unknown)
                    context.markImplicitConversion(*expr.rhs, target);
            }
        }
    }

    const auto &rule = semantic_analyzer_detail::exprRule(expr.op);
    if (rule.validator)
        rule.validator(context, expr, lhs, rhs, rule.mismatchDiag);
    if (rule.result)
        return rule.result(lhs, rhs);
    return Type::Unknown;
}

} // namespace il::frontends::basic::sem
