//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/TypeCoercionEngine.hpp
// Purpose: Centralized type coercion logic for BASIC-to-IL lowering.
//
// This module consolidates type coercion operations previously scattered
// across Lowerer, RuntimeStatementLowerer, and related components. It provides
// a single source of truth for BASIC type conversion semantics.
//
// BASIC Type Coercion Rules:
//   - Boolean (I1) to Integer (I64): Zero-extend (0 or -1 for TRUE)
//   - Float (F64) to Integer (I64): Round-to-even with overflow check
//   - Integer (I64) to Float (F64): Signed integer-to-float conversion
//   - Integer (I64) to Boolean (I1): Truncate to least significant bit
//   - Narrower integers (I16, I32) are sign-extended to I64 first
//
// Key Invariants:
//   - Coercions are idempotent (coercing to the same type is a no-op)
//   - All coercions preserve BASIC semantics (TRUE = -1, FALSE = 0)
//   - Overflow conditions generate checked conversion instructions
//
// Ownership/Lifetime:
//   - Stateless utility class; all state held in IRBuilder
//   - Methods receive builder and emit IL instructions directly
//
// Links: docs/architecture.md, docs/il-guide.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/LowererTypes.hpp"
#include "frontends/basic/ast/NodeFwd.hpp"
#include "support/source_location.hpp"
#include "viper/il/IRBuilder.hpp"
#include "viper/il/Module.hpp"
#include <functional>
#include <optional>
#include <string_view>

namespace il::frontends::basic
{

// Forward declarations
class Lowerer;

/// @brief Centralized type coercion engine for BASIC value conversions.
///
/// Provides a unified interface for converting values between BASIC types
/// during IL lowering. Consolidates conversion logic to ensure consistent
/// semantics across the frontend.
class TypeCoercionEngine
{
  public:
    using Value = il::core::Value;
    using Type = il::core::Type;
    using Opcode = il::core::Opcode;
    using AstType = ::il::frontends::basic::Type;
    using IlType = il::core::Type;

    /// @brief Construct a coercion engine bound to a lowerer.
    /// @param lowerer Lowerer providing IL emission context.
    explicit TypeCoercionEngine(Lowerer &lowerer) noexcept;

    // =========================================================================
    // Primary Coercion Methods
    // =========================================================================

    /// @brief Coerce a value to 64-bit signed integer.
    /// @param v Value/type pair to convert.
    /// @param loc Source location for emitted conversions.
    /// @return Updated value guaranteed to have i64 type.
    [[nodiscard]] RVal toI64(RVal v, il::support::SourceLoc loc);

    /// @brief Coerce a value to 64-bit floating-point.
    /// @param v Value/type pair to convert.
    /// @param loc Source location for emitted conversions.
    /// @return Updated value guaranteed to have f64 type.
    [[nodiscard]] RVal toF64(RVal v, il::support::SourceLoc loc);

    /// @brief Coerce a value to boolean (i1).
    /// @param v Value/type pair to convert.
    /// @param loc Source location for emitted conversions.
    /// @return Updated value guaranteed to have i1 type.
    [[nodiscard]] RVal toBool(RVal v, il::support::SourceLoc loc);

    /// @brief Coerce a value to a target IL type.
    /// @param v Value/type pair to convert.
    /// @param target Target IL type kind.
    /// @param loc Source location for emitted conversions.
    /// @return Updated value with the target type, or unchanged if no conversion needed.
    [[nodiscard]] RVal toType(RVal v, Type::Kind target, il::support::SourceLoc loc);

    /// @brief Coerce a value to match an AST type.
    /// @param v Value/type pair to convert.
    /// @param target Target AST type.
    /// @param loc Source location for emitted conversions.
    /// @return Updated value with appropriate IL type.
    [[nodiscard]] RVal toAstType(RVal v, AstType target, il::support::SourceLoc loc);

    // =========================================================================
    // Type Queries
    // =========================================================================

    /// @brief Check if a value is already of the target type.
    [[nodiscard]] static bool isI64(const RVal &v) noexcept;
    [[nodiscard]] static bool isF64(const RVal &v) noexcept;
    [[nodiscard]] static bool isBool(const RVal &v) noexcept;
    [[nodiscard]] static bool isString(const RVal &v) noexcept;
    [[nodiscard]] static bool isPointer(const RVal &v) noexcept;

    /// @brief Check if a type is numeric (integer or float).
    [[nodiscard]] static bool isNumeric(Type::Kind kind) noexcept;

    /// @brief Check if an AST type is numeric.
    [[nodiscard]] static bool isNumericAst(AstType type) noexcept;

    // =========================================================================
    // IL Type Helpers
    // =========================================================================

    /// @brief Get the IL boolean type (i1).
    [[nodiscard]] static IlType boolType() noexcept;

    /// @brief Get the IL integer type (i64).
    [[nodiscard]] static IlType intType() noexcept;

    /// @brief Get the IL float type (f64).
    [[nodiscard]] static IlType floatType() noexcept;

    /// @brief Get the IL pointer type.
    [[nodiscard]] static IlType ptrType() noexcept;

    /// @brief Convert an AST type to its corresponding IL type.
    [[nodiscard]] static IlType astToIl(AstType type) noexcept;

    // =========================================================================
    // Widening Helpers
    // =========================================================================

    /// @brief Sign-extend a narrower integer to 64 bits.
    /// @param v Value to widen.
    /// @param fromBits Original bit width (16 or 32).
    /// @param loc Source location.
    /// @return Widened value as i64.
    [[nodiscard]] Value widenToI64(Value v, int fromBits, il::support::SourceLoc loc);

    // =========================================================================
    // Promotion Rules
    // =========================================================================

    /// @brief Determine the common type for binary operations.
    /// @details Implements BASIC numeric promotion rules:
    ///          - If either operand is float, result is float
    ///          - Otherwise, result is integer
    /// @param lhs Left operand type.
    /// @param rhs Right operand type.
    /// @return Common type for the operation.
    [[nodiscard]] static Type::Kind promoteNumeric(Type::Kind lhs, Type::Kind rhs) noexcept;

    /// @brief Coerce both operands to a common type.
    /// @param lhs Left operand (modified in place).
    /// @param rhs Right operand (modified in place).
    /// @param loc Source location for emitted conversions.
    void promoteOperands(RVal &lhs, RVal &rhs, il::support::SourceLoc loc);

  private:
    Lowerer &lowerer_;

    // Internal helpers for IL emission
    [[nodiscard]] Value emitUnary(Opcode op, Type resultType, Value operand);
    [[nodiscard]] Value emitBoolToLogicalI64(Value boolVal);
};

} // namespace il::frontends::basic
