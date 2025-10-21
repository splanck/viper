//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Encapsulates BASIC numeric type promotion rules. The helpers codify operator
// result types so the semantic analyzer and lowerer can share a single source of
// truth when reasoning about integer versus floating-point behaviour.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements BASIC numeric promotion rules for operators.
/// @details The semantic analyzer consults these routines when resolving result
///          types or reporting incompatibilities. Centralising the logic avoids
///          drift between diagnostics and code generation.

#include "frontends/basic/TypeRules.hpp"

#include <array>
#include <string>
#include <utility>

namespace il::frontends::basic
{
namespace
{
using NumericType = TypeRules::NumericType;
using BinaryFn = NumericType (*)(NumericType, NumericType) noexcept;

/// @brief Access the global type error sink used by TypeRules.
///
/// @details The sink is a process-wide callback that receives recoverable type
///          errors. A default no-op is provided to avoid nullptr checks.
///
/// @return Reference to the sink, creating it if necessary.
TypeRules::TypeErrorSink &typeErrorSink() noexcept
{
    static TypeRules::TypeErrorSink sink;
    return sink;
}

/// @brief Produce a BASIC-facing name for a numeric type enumerant.
///
/// @param type Numeric type value.
/// @return Uppercase string describing @p type.
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

/// @brief Emit a recoverable type error through the registered sink.
///
/// @param code Diagnostic code identifying the issue.
/// @param message Human-readable description.
void emitTypeError(std::string code, std::string message) noexcept
{
    if (auto &sink = typeErrorSink())
    {
        sink(TypeRules::TypeError{std::move(code), std::move(message)});
    }
}

/// @brief Report an unsupported binary operator pairing.
///
/// @param op Operator string encountered.
/// @param lhs Left operand type.
/// @param rhs Right operand type.
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
///
/// @param op Operator character.
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

/// @brief Report an unsupported operand type for a unary operator.
///
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

/// @brief Determine whether a numeric type is integral.
///
/// @param type Numeric type to classify.
/// @return True if @p type represents an integer category.
constexpr bool isInteger(NumericType type) noexcept
{
    return type == NumericType::Integer || type == NumericType::Long;
}

/// @brief Determine whether a numeric type is floating point.
///
/// @param type Numeric type to classify.
/// @return True when @p type is SINGLE or DOUBLE.
constexpr bool isFloat(NumericType type) noexcept
{
    return type == NumericType::Single || type == NumericType::Double;
}

/// @brief Promote two integer types to their common result type.
///
/// @param lhs Left operand type.
/// @param rhs Right operand type.
/// @return Resulting integer type after promotion.
constexpr NumericType promoteInteger(NumericType lhs, NumericType rhs) noexcept
{
    return (lhs == NumericType::Long || rhs == NumericType::Long) ? NumericType::Long
                                                                  : NumericType::Integer;
}

/// @brief Promote two floating-point types to their common result type.
///
/// @param lhs Left operand type.
/// @param rhs Right operand type.
/// @return Resulting floating type after promotion.
constexpr NumericType promoteFloat(NumericType lhs, NumericType rhs) noexcept
{
    return (lhs == NumericType::Double || rhs == NumericType::Double) ? NumericType::Double
                                                                      : NumericType::Single;
}

/// @brief Compute the result type for arithmetic operators (+, -, *).
///
/// @param lhs Left operand type.
/// @param rhs Right operand type.
/// @return Promoted numeric type respecting integer/float rules.
constexpr NumericType arithmeticResult(NumericType lhs, NumericType rhs) noexcept
{
    if (isInteger(lhs) && isInteger(rhs))
        return promoteInteger(lhs, rhs);
    return promoteFloat(lhs, rhs);
}

/// @brief Compute the result type for floating-point division.
///
/// @param lhs Left operand type.
/// @param rhs Right operand type.
/// @return Result type mandated by BASIC for `/`.
constexpr NumericType divisionResult(NumericType lhs, NumericType rhs) noexcept
{
    if (lhs == NumericType::Double || rhs == NumericType::Double)
        return NumericType::Double;
    if (lhs == NumericType::Single || rhs == NumericType::Single)
        return NumericType::Single;
    return NumericType::Double;
}

/// @brief Compute the result type for integer division-like operators.
///
/// @param lhs Left operand type.
/// @param rhs Right operand type.
/// @return Promoted integer type.
constexpr NumericType integerResult(NumericType lhs, NumericType rhs) noexcept
{
    return promoteInteger(lhs, rhs);
}

/// @brief Compute the result type for exponentiation.
///
/// @return BASIC mandates DOUBLE precision results for `^`.
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

/// @brief Convert a character to uppercase without locale influence.
///
/// @param c Character to convert.
/// @return Uppercase equivalent when alphabetic, otherwise original value.
constexpr char upperChar(char c) noexcept
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

/// @brief Case-insensitive string comparison for ASCII operator names.
///
/// @param lhs First string.
/// @param rhs Second string.
/// @return True when strings match ignoring ASCII case.
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

/// @brief Install a callback that receives recoverable type errors.
///
/// @param sink Callable invoked when a recoverable error occurs.
void TypeRules::setTypeErrorSink(TypeErrorSink sink) noexcept
{
    typeErrorSink() = std::move(sink);
}

/// @brief Determine the result type of a binary numeric operator.
///
/// @details Handles multi-character operators (e.g., `MOD`) before deferring to
///          the single-character overload. Unsupported operators emit a
///          diagnostic and fall back to @p lhs.
///
/// @param op Operator token as text.
/// @param lhs Left operand type.
/// @param rhs Right operand type.
/// @return Resulting numeric type according to BASIC promotion rules.
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

/// @brief Determine the result type of a single-character operator.
///
/// @details Consults the rule table and, on failure, emits an error before
///          returning @p lhs.
///
/// @param op Operator character.
/// @param lhs Left operand type.
/// @param rhs Right operand type.
/// @return Result type per BASIC promotion rules.
TypeRules::NumericType TypeRules::resultType(char op,
                                             NumericType lhs,
                                             NumericType rhs) noexcept
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

/// @brief Determine the result type of a unary numeric operator.
///
/// @details Supports unary `+` and `-` for integer/float operands. Unsupported
///          combinations trigger diagnostics and preserve the operand type to
///          minimise cascading failures.
///
/// @param op Operator character.
/// @param operand Operand type.
/// @return Result type for the unary operation.
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
