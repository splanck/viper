//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/EmitCommon.hpp
// Purpose: Declare shared IR emission helpers to reduce boilerplate across the
//          BASIC front-end.
// Key invariants: Helpers honour the active Lowerer context and only append
//                 instructions to the current block when one is selected.
// Ownership/Lifetime: Emit borrows the Lowerer instance; IR objects remain
//                     owned by the lowering pipeline.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Instr.hpp"
#include "support/source_location.hpp"

#include <cstdint>
#include <optional>

namespace il::frontends::basic
{

class Lowerer;

/// @brief Selects how overflow should be handled for arithmetic helpers.
enum class OverflowPolicy
{
    Checked, ///< Emit overflow-checking arithmetic.
    Wrap     ///< Emit wrapping arithmetic.
};

/// @brief Indicates whether widening preserves signedness or zero-extends.
enum class Signedness
{
    Signed,
    Unsigned
};

/// @brief Facade that centralises common IL emission patterns for the BASIC front end.
/// @invariant Each helper assumes the caller prepared the Lowerer with an active block.
/// @ownership Does not own IR objects; borrows the Lowerer state for instruction emission.
class Emit
{
  public:
    using Value = il::core::Value;
    using Type = il::core::Type;
    using Opcode = il::core::Opcode;

    explicit Emit(Lowerer &lowerer) noexcept;

    /// @brief Annotate subsequent emissions with @p loc.
    Emit &at(il::support::SourceLoc loc) noexcept;

    /// @brief Convert @p value from @p fromBits to @p bits.
    [[nodiscard]] Value to_iN(Value value,
                              int bits,
                              int fromBits = 64,
                              Signedness signedness = Signedness::Signed) const;

    /// @brief Widen @p value from @p fromBits to @p toBits.
    [[nodiscard]] Value widen_to(Value value,
                                 int fromBits,
                                 int toBits,
                                 Signedness signedness = Signedness::Signed) const;

    /// @brief Narrow @p value from @p fromBits to @p toBits with overflow checking.
    [[nodiscard]] Value narrow_to(Value value, int fromBits, int toBits) const;

    /// @brief Emit an integer addition following the requested overflow policy.
    [[nodiscard]] Value add_checked(Value lhs,
                                    Value rhs,
                                    OverflowPolicy policy,
                                    int bits = 64) const;

    /// @brief Emit a logical AND on BASIC logical masks.
    [[nodiscard]] Value logical_and(Value lhs, Value rhs, int bits = 64) const;

    /// @brief Emit a logical OR on BASIC logical masks.
    [[nodiscard]] Value logical_or(Value lhs, Value rhs, int bits = 64) const;

    /// @brief Emit a logical XOR on BASIC logical masks.
    [[nodiscard]] Value logical_xor(Value lhs, Value rhs, int bits = 64) const;

  private:
    Lowerer *lowerer_;
    mutable std::optional<il::support::SourceLoc> loc_;

    [[nodiscard]] Type intType(int bits) const;
    void applyLoc() const;
    [[nodiscard]] Value emitUnary(Opcode op, Type ty, Value val) const;
    [[nodiscard]] Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs) const;
};

} // namespace il::frontends::basic
