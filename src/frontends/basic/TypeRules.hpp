// File: src/frontends/basic/TypeRules.hpp
// Purpose: Declares BASIC numeric type promotion and operator result rules.
// Key invariants: Operator tables implement the INTEGER/LONG/SINGLE/DOUBLE lattice.
// Ownership/Lifetime: Pure stateless utility; no retained resources.
// Links: docs/codemap.md
#pragma once

#include <string_view>

namespace il::frontends::basic
{

/// @brief Numeric BASIC scalar types.
/// @details INTEGER and LONG are integral; SINGLE and DOUBLE are floating-point.
class TypeRules
{
  public:
    /// @brief Available numeric types ordered by promotion lattice.
    enum class NumericType
    {
        Integer, ///< 16-bit signed integer.
        Long,    ///< 32-bit signed integer.
        Single,  ///< 32-bit IEEE-754 floating-point.
        Double,  ///< 64-bit IEEE-754 floating-point.
    };

    /// @brief Determine the binary operator result type.
    /// @param op Operator token ("+", "-", "*", "/", "\\", "MOD", "^").
    /// @param lhs Left operand numeric type.
    /// @param rhs Right operand numeric type.
    /// @return Resulting numeric type according to BASIC promotion rules.
    static NumericType resultType(std::string_view op, NumericType lhs, NumericType rhs) noexcept;

    /// @brief Determine binary operator result type for single-character operators.
    static NumericType resultType(char op, NumericType lhs, NumericType rhs) noexcept;

    /// @brief Determine the unary operator result type.
    /// @param op Unary operator (currently only '-').
    /// @param operand Operand numeric type.
    static NumericType unaryResultType(char op, NumericType operand) noexcept;
};

} // namespace il::frontends::basic
