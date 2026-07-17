//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the TypeRules class, which implements BASIC's numeric
// type promotion and operator result type computation rules.
//
// BASIC Numeric Type Lattice:
// BASIC preserves the surface promotion hierarchy for diagnostics and result
// spelling:
//   Integer → Long → Single → Double
// The current lowering maps integer results to i64 and floating results to f64;
// see docs/specs/numerics.md for the normative storage contract.
//
// Type Promotion Rules:
// When binary operators combine operands of different numeric types, BASIC
// promotes the result to the wider type:
//   Integer + Long   → Long
//   Long + Single    → Single
//   Single + Double  → Double
//   Integer * Double → Double
//
// These rules keep the frontend's semantic result names stable while lowering
// uses the implemented IL storage widths.
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
// - Exponentiation (^): Produces Double
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

namespace il::frontends::basic {

/// @brief Numeric BASIC scalar types.
/// @details INTEGER and LONG are integral; SINGLE and DOUBLE are floating-point.
class TypeRules {
  public:
    /// @brief Available numeric types ordered by promotion lattice.
    enum class NumericType {
        Integer, ///< INTEGER/INT surface rank, stored as i64.
        Long,    ///< LONG surface rank, stored as i64.
        Single,  ///< SINGLE surface rank, stored as f64.
        Double,  ///< DOUBLE surface rank, stored as f64.
    };

    /// @brief Structured information describing a numeric type error.
    struct TypeError {
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
