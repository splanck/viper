//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/ValueKey.hpp
// Purpose: Internal helpers for computing expression identity keys used by
//          EarlyCSE and GVN. Normalises commutative operands, provides stable
//          hashing for Value operands, and gates which opcodes are safe for
//          CSE (pure, non-trapping, no memory effects).
// Key invariants:
//   - Commutative operands are sorted to produce canonical keys.
//   - Only pure, non-trapping, non-memory opcodes pass isSafeCSEOpcode().
// Ownership/Lifetime: ValueKey is a value type owning a vector of operands.
//          Hash and equality functors are stateless.
// Links: il/core/Instr.hpp, il/core/OpcodeInfo.hpp, il/core/Value.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace il::transform
{

/// @brief Hash a Value by kind and payload for use in expression keys.
struct ValueHash
{
    size_t operator()(const il::core::Value &v) const noexcept;
};

/// @brief Equality on Value payloads (ignores name metadata).
struct ValueEq
{
    bool operator()(const il::core::Value &a, const il::core::Value &b) const noexcept;
};

/// @brief Normalised key describing a pure instruction.
struct ValueKey
{
    il::core::Opcode op;
    il::core::Type::Kind type;
    std::vector<il::core::Value> operands;

    bool operator==(const ValueKey &o) const noexcept;
};

struct ValueKeyHash
{
    size_t operator()(const ValueKey &k) const noexcept;
};

/// @brief Returns true when @p op is treated as commutative for CSE purposes.
bool isCommutativeCSE(il::core::Opcode op) noexcept;

/// @brief Returns true if @p op is safe for expression-based CSE/GVN.
/// @details Only includes pure, non-trapping, non-memory ops (integer/FP
///          arithmetic without traps, bitwise ops, compares, boolean casts).
bool isSafeCSEOpcode(il::core::Opcode op) noexcept;

/// @brief Build a normalised expression key for @p instr when eligible.
/// @return Populated ValueKey or std::nullopt when @p instr is not a CSE
///         candidate (side effects, memory, disallowed opcode, missing result).
std::optional<ValueKey> makeValueKey(const il::core::Instr &instr);

} // namespace il::transform
