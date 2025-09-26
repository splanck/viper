// File: src/frontends/basic/TypeRules.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements BASIC numeric type promotion and operator result rules.
// Key invariants: Operator lookup table reflects INTEGER/LONG/SINGLE/DOUBLE semantics.
// Ownership/Lifetime: Stateless helpers.
// Links: docs/codemap.md

#include "frontends/basic/TypeRules.hpp"

#include <array>
#include <cassert>

namespace il::frontends::basic
{
namespace
{
using NumericType = TypeRules::NumericType;
using BinaryFn = NumericType (*)(NumericType, NumericType) noexcept;

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
    assert(false && "Unsupported operator");
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
    assert(false && "Unsupported operator");
    return lhs;
}

TypeRules::NumericType TypeRules::unaryResultType(char op, NumericType operand) noexcept
{
    assert(op == '-' && "Only unary minus supported");
    if (isFloat(operand) || isInteger(operand))
        return operand;
    assert(false && "Unsupported operand type for unary minus");
    return operand;
}

} // namespace il::frontends::basic
