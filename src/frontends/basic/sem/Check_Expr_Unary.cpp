//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
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

constexpr bool isNumericType(Type type) noexcept
{
    return type == Type::Int || type == Type::Float || type == Type::Unknown;
}
} // namespace

SemanticAnalyzer::Type analyzeUnaryExpr(SemanticAnalyzer &analyzer, const UnaryExpr &expr)
{
    ExprCheckContext context(analyzer);
    Type operandType = Type::Unknown;
    if (expr.expr)
        operandType = context.evaluate(*expr.expr);

    switch (expr.op)
    {
        case UnaryExpr::Op::LogicalNot:
            if (operandType != Type::Unknown && operandType != Type::Bool)
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
            return Type::Bool;

        case UnaryExpr::Op::Plus:
        case UnaryExpr::Op::Negate:
            if (operandType != Type::Unknown && !isNumericType(operandType))
            {
                std::ostringstream oss;
                const char opChar = (expr.op == UnaryExpr::Op::Negate) ? '-' : '+';
                oss << "unary " << opChar << " requires a NUMERIC operand, got "
                    << semantic_analyzer_detail::semanticTypeName(operandType) << '.';
                emitTypeMismatch(context.diagnostics(),
                                 "B2001",
                                 expr.loc,
                                 1,
                                 oss.str());
                return Type::Unknown;
            }
            return operandType;
    }

    return Type::Unknown;
}

} // namespace il::frontends::basic::sem
