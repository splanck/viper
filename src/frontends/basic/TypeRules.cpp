// File: src/frontends/basic/TypeRules.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements BASIC numeric type promotion and operator result rules.
// Key invariants: Operator lookup table reflects INTEGER/LONG/SINGLE/DOUBLE semantics.
// Ownership/Lifetime: Stateless helpers.
// Links: docs/codemap.md

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

TypeRules::TypeErrorSink &typeErrorSink() noexcept
{
    static TypeRules::TypeErrorSink sink;
    return sink;
}

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

void emitTypeError(std::string code, std::string message) noexcept
{
    if (auto &sink = typeErrorSink())
    {
        sink(TypeRules::TypeError{std::move(code), std::move(message)});
    }
}

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

void reportUnsupportedUnaryOperator(char op, NumericType operand) noexcept
{
    std::string message = "unsupported unary operator '";
    message.push_back(op);
    message += "' for operand ";
    message += numericTypeName(operand);
    message += '.';
    emitTypeError("B2102", std::move(message));
}

void reportUnsupportedUnaryOperand(char op, NumericType operand) noexcept
{
    std::string message = "unsupported operand ";
    message += numericTypeName(operand);
    message += " for unary operator '";
    message.push_back(op);
    message += "'.";
    emitTypeError("B2103", std::move(message));
}

constexpr bool isInteger(NumericType type) noexcept
{
    return type == NumericType::Integer || type == NumericType::Long;
}

constexpr bool isFloat(NumericType type) noexcept
{
    return type == NumericType::Single || type == NumericType::Double;
}

constexpr NumericType promoteInteger(NumericType lhs, NumericType rhs) noexcept
{
    return (lhs == NumericType::Long || rhs == NumericType::Long) ? NumericType::Long
                                                                  : NumericType::Integer;
}

constexpr NumericType promoteFloat(NumericType lhs, NumericType rhs) noexcept
{
    return (lhs == NumericType::Double || rhs == NumericType::Double) ? NumericType::Double
                                                                      : NumericType::Single;
}

constexpr NumericType arithmeticResult(NumericType lhs, NumericType rhs) noexcept
{
    if (isInteger(lhs) && isInteger(rhs))
        return promoteInteger(lhs, rhs);
    return promoteFloat(lhs, rhs);
}

constexpr NumericType divisionResult(NumericType lhs, NumericType rhs) noexcept
{
    if (lhs == NumericType::Double || rhs == NumericType::Double)
        return NumericType::Double;
    if (lhs == NumericType::Single || rhs == NumericType::Single)
        return NumericType::Single;
    return NumericType::Double;
}

constexpr NumericType integerResult(NumericType lhs, NumericType rhs) noexcept
{
    return promoteInteger(lhs, rhs);
}

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

constexpr char upperChar(char c) noexcept
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

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

void TypeRules::setTypeErrorSink(TypeErrorSink sink) noexcept
{
    typeErrorSink() = std::move(sink);
}

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
