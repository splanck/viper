//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/sem/Check_Expr_Unary.cpp
// Purpose: Perform BASIC semantic validation for unary expression nodes while
//          wiring diagnostics and implicit conversions into the analyzer state.
// Key invariants:
//   * Unary operators only accept operand types permitted by the language spec;
//     mismatches produce rich diagnostics referencing the source operand.
//   * Successful checks update @ref ExprCheckContext so later phases know about
//     implicit numeric promotions and maintain consistent scope bookkeeping.
// References: docs/basic-language.md#expressions, docs/codemap/basic.md
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic analysis helpers for unary expressions.
/// @details Evaluates operand types and emits diagnostics for invalid
///          combinations while delegating shared reporting to Check_Common
///          helpers.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/Check_Common.hpp"

#include <sstream>

namespace il::frontends::basic::sem
{
namespace
{
using Type = SemanticAnalyzer::Type;

/// @brief Check if the provided semantic type is treated as numeric.
///
/// The helper recognises integer, floating-point, and unknown operands.  Unknown
/// operands are tolerated because earlier failures may already have produced
/// diagnostics, and the analyzer defers further reporting to avoid cascading
/// errors.  Keeping this logic centralised ensures the same definition of
/// "numeric" is reused for both diagnostic emission and result inference.
///
/// @param type Semantic type associated with the operand expression.
/// @return True when @p type is integer, float, or unknown.
constexpr bool isNumericType(Type type) noexcept
{
    return type == Type::Int || type == Type::Float || type == Type::Unknown;
}
} // namespace

/// @brief Analyse a unary BASIC expression and determine its resulting type.
///
/// The routine establishes an @ref ExprCheckContext to manage implicit
/// conversions, scope guards, and diagnostic routing.  It evaluates the operand
/// (if present) to recover its semantic type, then applies operator-specific
/// rules:
///
/// * `NOT` accepts boolean or unknown operands; non-boolean operands emit
///   diagnostic B2001 describing the mismatch before returning @c Bool.
/// * Arithmetic negation and prefix plus require numeric operands.  When the
///   operand type is known and non-numeric, the analyzer emits B2001 and halts
///   further checking for that expression.
///
/// Unknown operands bypass additional diagnostics to avoid cascading messages
/// when earlier stages already reported issues.  Valid operands produce the
/// appropriate resulting type according to the language rules, enabling later
/// passes to perform code generation without re-deriving the information.
///
/// @param analyzer Semantic analyzer coordinating validation.
/// @param expr Unary expression AST node being checked.
/// @return Type category of the expression, or Unknown when diagnostics prevent
///         classification.
SemanticAnalyzer::Type analyzeUnaryExpr(SemanticAnalyzer &analyzer, const UnaryExpr &expr)
{
    ExprCheckContext context(analyzer);
    Type operandType = Type::Unknown;
    if (expr.expr)
        operandType = context.evaluate(*expr.expr);

    switch (expr.op)
    {
        case UnaryExpr::Op::LogicalNot:
            // NOT accepts BOOLEAN or INTEGER
            if (operandType != Type::Unknown && operandType != Type::Bool &&
                operandType != Type::Int)
            {
                std::ostringstream oss;
                oss << "NOT requires a BOOLEAN operand, got "
                    << semantic_analyzer_detail::semanticTypeName(operandType) << '.';
                emitTypeMismatch(context.diagnostics(),
                                 std::string(SemanticAnalyzer::DiagNonBooleanNotOperand),
                                 expr.loc,
                                 3,
                                 oss.str());
            }
            // NOT returns BOOLEAN when applied to BOOLEAN, INT otherwise
            return (operandType == Type::Bool) ? Type::Bool : Type::Int;

        case UnaryExpr::Op::Plus:
        case UnaryExpr::Op::Negate:
            if (operandType != Type::Unknown && !isNumericType(operandType))
            {
                std::ostringstream oss;
                const char opChar = (expr.op == UnaryExpr::Op::Negate) ? '-' : '+';
                oss << "unary " << opChar << " requires a NUMERIC operand, got "
                    << semantic_analyzer_detail::semanticTypeName(operandType) << '.';
                emitTypeMismatch(context.diagnostics(), "B2001", expr.loc, 1, oss.str());
                return Type::Unknown;
            }
            return operandType;
    }

    return Type::Unknown;
}

} // namespace il::frontends::basic::sem
