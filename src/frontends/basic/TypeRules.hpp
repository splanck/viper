//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the TypeRules class, which implements BASIC's numeric
// type promotion and operator result type computation rules.
//
// BASIC Numeric Type Lattice:
// BASIC defines a type promotion hierarchy for numeric operations:
//   Integer (16-bit) → Long (32-bit) → Single (32-bit float) → Double (64-bit float)
//
// Type Promotion Rules:
// When binary operators combine operands of different numeric types, BASIC
// promotes the result to the wider type:
//   Integer + Long   → Long
//   Long + Single    → Single
//   Single + Double  → Double
//   Integer * Double → Double
//
// These rules ensure that precision is never lost implicitly in numeric
// expressions, matching the behavior of classic BASIC implementations.
//
// Key Responsibilities:
// - Type promotion: Determines the result type of binary numeric operations
// - Operator validation: Checks whether a given operator is valid for the
//   operand types (e.g., MOD requires integer types)
// - Error reporting: Generates descriptive type error messages when operations
//   are invalid (e.g., using MOD with floating-point operands)
// - Division semantics: Distinguishes between integer division (\) and
//   floating-point division (/)
//
// Operator-Specific Rules:
// - Arithmetic (+, -, *, /): Follow standard promotion lattice
// - Integer division (\): Requires both operands to be Integer or Long;
//   result is always Integer or Long
// - Modulo (MOD): Requires both operands to be Integer or Long
// - Exponentiation (^): Promotes to Single or Double based on operands
// - Comparison (=, <, >, <=, >=, <>): Operands promoted for comparison,
//   result is always Boolean (represented as Integer in BASIC)
//
// Integration:
// - Used by: SemanticAnalyzer during expression type checking
// - Used by: Lowerer to determine IL type for operation results
// - Consulted during: Binary expression validation in the semantic pass
//
// Design Notes:
// - Stateless utility class; all methods are pure functions
// - No retained resources or mutable state
// - Lookup tables enable efficient type promotion computation
// - Error messages provide context for fixing type mismatches
//
//===----------------------------------------------------------------------===//
#pragma once

#include <functional>
#include <string>
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

    /// @brief Structured information describing a numeric type error.
    struct TypeError
    {
        std::string code;    ///< Project-defined diagnostic code.
        std::string message; ///< Human-readable explanation.
    };

    /// @brief Callback invoked when recoverable type errors occur.
    using TypeErrorSink = std::function<void(const TypeError &error)>;

    /// @brief Determine the binary operator result type.
    /// @param op Operator token ("+", "-", "*", "/", "\\", "MOD", "^").
    /// @param lhs Left operand numeric type.
    /// @param rhs Right operand numeric type.
    /// @return Resulting numeric type according to BASIC promotion rules.
    static NumericType resultType(std::string_view op, NumericType lhs, NumericType rhs) noexcept;

    /// @brief Determine binary operator result type for single-character operators.
    static NumericType resultType(char op, NumericType lhs, NumericType rhs) noexcept;

    /// @brief Determine the unary operator result type.
    /// @param op Unary operator ("-" or "+").
    /// @param operand Operand numeric type.
    static NumericType unaryResultType(char op, NumericType operand) noexcept;

    /// @brief Install a callback used to report recoverable type errors.
    static void setTypeErrorSink(TypeErrorSink sink) noexcept;
};

} // namespace il::frontends::basic
