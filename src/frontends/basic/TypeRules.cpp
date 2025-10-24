//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC numeric type promotion rules used during semantic
// analysis.  The helpers centralise the mapping between operator spellings and
// resulting numeric types, emitting diagnostics for unsupported combinations and
// giving later phases consistent expectations about operand coercions.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/TypeRules.hpp"

#include <array>
#include <string>
#include <utility>

/// @file
/// @brief Numeric promotion utilities for the BASIC front end.
/// @details Provides helper functions that compute result types for unary and
///          binary numeric operators while surfacing diagnostics when operands or
///          operators are unsupported.  The implementation mirrors the original
///          BASIC promotion rules so lowering and runtime calls can assume
///          consistent behaviour.

namespace il::frontends::basic
{
namespace
{
using NumericType = TypeRules::NumericType;
using BinaryFn = NumericType (*)(NumericType, NumericType) noexcept;

/// @brief Access the globally configured type error sink.
/// @details Lazily initialises the sink to an empty callable so callers can
///          install a handler without worrying about static initialisation order.
/// @return Reference to the stored sink function.
TypeRules::TypeErrorSink &typeErrorSink() noexcept
{
    static TypeRules::TypeErrorSink sink;
    return sink;
}

/// @brief Convert a numeric type enumerator into a human-readable string.
/// @param type Numeric type to describe.
/// @return Uppercase name for the BASIC numeric type.
std::string_view numericTypeName(NumericType type) noexcept
{
    switch (type)
    {
        case NumericType::Integer:
            return "INTEGER";
        case NumericType::Long:
            return "LONG";
        case NumericType::Single:
            return "SINGLE";
        case NumericType::Double:
            return "DOUBLE";
    }
    return "UNKNOWN";
}

/// @brief Emit a diagnostic for a type error if a sink is configured.
/// @param code Diagnostic identifier describing the error.
/// @param message Human-readable explanation of the violation.
void emitTypeError(std::string code, std::string message) noexcept
{
    if (auto &sink = typeErrorSink())
    {
        sink(TypeRules::TypeError{std::move(code), std::move(message)});
    }
}

/// @brief Report an unsupported binary operator and operand combination.
/// @param op Operator spelling.
/// @param lhs Left-hand operand type.
/// @param rhs Right-hand operand type.
void reportUnsupportedBinary(std::string_view op, NumericType lhs, NumericType rhs) noexcept
{
    std::string message = "unsupported numeric operator '";
    message += op;
    message += "' for operands ";
    message += numericTypeName(lhs);
    message += " and ";
    message += numericTypeName(rhs);
    message += '.';
    emitTypeError("B2101", std::move(message));
}

/// @brief Report an unsupported unary operator.
/// @param op Operator character (for example '+').
/// @param operand Operand type.
void reportUnsupportedUnaryOperator(char op, NumericType operand) noexcept
{
    std::string message = "unsupported unary operator '";
    message.push_back(op);
    message += "' for operand ";
    message += numericTypeName(operand);
    message += '.';
    emitTypeError("B2102", std::move(message));
}

/// @brief Report an unsupported unary operand for a valid operator.
/// @param op Operator character.
/// @param operand Operand type.
void reportUnsupportedUnaryOperand(char op, NumericType operand) noexcept
{
    std::string message = "unsupported operand ";
    message += numericTypeName(operand);
    message += " for unary operator '";
    message.push_back(op);
    message += "'.";
    emitTypeError("B2103", std::move(message));
}

/// @brief Check whether the numeric type is an integer category.
/// @param type Numeric type to query.
/// @return True if the type is INTEGER or LONG.
constexpr bool isInteger(NumericType type) noexcept
{
    return type == NumericType::Integer || type == NumericType::Long;
}

/// @brief Check whether the numeric type is a floating-point category.
/// @param type Numeric type to query.
/// @return True if the type is SINGLE or DOUBLE.
constexpr bool isFloat(NumericType type) noexcept
{
    return type == NumericType::Single || type == NumericType::Double;
}

/// @brief Promote two integer operands to a common integer type.
/// @param lhs Left-hand operand type.
/// @param rhs Right-hand operand type.
/// @return INTEGER when both operands are INTEGER, otherwise LONG.
constexpr NumericType promoteInteger(NumericType lhs, NumericType rhs) noexcept
{
    return (lhs == NumericType::Long || rhs == NumericType::Long) ? NumericType::Long
                                                                  : NumericType::Integer;
}

/// @brief Promote two floating-point operands to a common type.
/// @param lhs Left-hand operand type.
/// @param rhs Right-hand operand type.
/// @return DOUBLE when either operand is DOUBLE, otherwise SINGLE.
constexpr NumericType promoteFloat(NumericType lhs, NumericType rhs) noexcept
{
    return (lhs == NumericType::Double || rhs == NumericType::Double) ? NumericType::Double
                                                                      : NumericType::Single;
}

/// @brief Determine the result type for arithmetic operators (+, -, *).
/// @param lhs Left-hand operand type.
/// @param rhs Right-hand operand type.
/// @return Promoted integer type when both operands are integers; otherwise a promoted float type.
constexpr NumericType arithmeticResult(NumericType lhs, NumericType rhs) noexcept
{
    if (isInteger(lhs) && isInteger(rhs))
        return promoteInteger(lhs, rhs);
    return promoteFloat(lhs, rhs);
}

/// @brief Determine the result type for division operators (/).
/// @param lhs Left-hand operand type.
/// @param rhs Right-hand operand type.
/// @return DOUBLE when either operand is DOUBLE, SINGLE when either operand is SINGLE, otherwise
/// DOUBLE.
constexpr NumericType divisionResult(NumericType lhs, NumericType rhs) noexcept
{
    if (lhs == NumericType::Double || rhs == NumericType::Double)
        return NumericType::Double;
    if (lhs == NumericType::Single || rhs == NumericType::Single)
        return NumericType::Single;
    return NumericType::Double;
}

/// @brief Determine the result type for integer division and modulus.
/// @param lhs Left-hand operand type.
/// @param rhs Right-hand operand type.
/// @return Promoted integer type.
constexpr NumericType integerResult(NumericType lhs, NumericType rhs) noexcept
{
    return promoteInteger(lhs, rhs);
}

/// @brief Determine the result type for exponentiation.
/// @return DOUBLE regardless of operand types, matching BASIC semantics.
constexpr NumericType powerResult(NumericType, NumericType) noexcept
{
    return NumericType::Double;
}

struct BinaryRule
{
    std::string_view op;
    BinaryFn fn;
};

constexpr std::array<BinaryRule, 7> Rules = {{{"+", &arithmeticResult},
                                              {"-", &arithmeticResult},
                                              {"*", &arithmeticResult},
                                              {"/", &divisionResult},
                                              {"\\", &integerResult},
                                              {"MOD", &integerResult},
                                              {"^", &powerResult}}};

/// @brief Uppercase an ASCII character.
/// @param c Character to convert.
/// @return Uppercase equivalent when @p c is lowercase; otherwise @p c unchanged.
constexpr char upperChar(char c) noexcept
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

/// @brief Compare two strings ignoring ASCII case.
/// @param lhs Left-hand string.
/// @param rhs Right-hand string.
/// @return True if the strings are equal when compared case-insensitively.
constexpr bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (upperChar(lhs[i]) != upperChar(rhs[i]))
            return false;
    }
    return true;
}

} // namespace

/// @brief Install a callback that receives type error diagnostics.
/// @param sink Callable invoked with @ref TypeRules::TypeError payloads.
void TypeRules::setTypeErrorSink(TypeErrorSink sink) noexcept
{
    typeErrorSink() = std::move(sink);
}

/// @brief Determine the result numeric type for a binary operator.
/// @details Looks up the operator spelling in the @ref Rules table and invokes
///          the associated function to compute the promotion result.  When the
///          operator is unknown the function emits a diagnostic and falls back to
///          the left-hand operand type.
/// @param op Operator spelling (case-insensitive).
/// @param lhs Left-hand operand type.
/// @param rhs Right-hand operand type.
/// @return Resulting numeric type.
TypeRules::NumericType TypeRules::resultType(std::string_view op,
                                             NumericType lhs,
                                             NumericType rhs) noexcept
{
    if (op.size() == 1)
        return resultType(op.front(), lhs, rhs);

    for (const auto &rule : Rules)
    {
        if (equalsIgnoreCase(rule.op, op))
            return rule.fn(lhs, rhs);
    }
    // Recoverable path: emit diagnostic and fall back to lhs type.
    reportUnsupportedBinary(op, lhs, rhs);
    return lhs;
}

/// @brief Determine the result numeric type for a single-character operator.
/// @details Searches the @ref Rules table for a matching single-character
///          operator.  When the operator is unknown the function emits a
///          diagnostic and falls back to the left-hand operand type.
/// @param op Operator character.
/// @param lhs Left-hand operand type.
/// @param rhs Right-hand operand type.
/// @return Resulting numeric type.
TypeRules::NumericType TypeRules::resultType(char op, NumericType lhs, NumericType rhs) noexcept
{
    for (const auto &rule : Rules)
    {
        if (rule.op.size() == 1 && rule.op.front() == op)
            return rule.fn(lhs, rhs);
    }
    // Recoverable path: emit diagnostic and fall back to lhs type.
    std::string opStr(1, op);
    reportUnsupportedBinary(opStr, lhs, rhs);
    return lhs;
}

/// @brief Determine the result numeric type for a unary operator.
/// @details Supports the @c + and @c - operators for numeric operands.  When an
///          unsupported operator or operand type is encountered, a diagnostic is
///          emitted and the operand type is returned unchanged.
/// @param op Operator character.
/// @param operand Operand type.
/// @return Resulting numeric type after applying the operator.
TypeRules::NumericType TypeRules::unaryResultType(char op, NumericType operand) noexcept
{
    if (op == '-' || op == '+')
    {
        if (isFloat(operand) || isInteger(operand))
            return operand;
        // Recoverable path: emit diagnostic and preserve operand type.
        reportUnsupportedUnaryOperand(op, operand);
        return operand;
    }
    // Recoverable path: emit diagnostic and preserve operand type.
    reportUnsupportedUnaryOperator(op, operand);
    return operand;
}

} // namespace il::frontends::basic
