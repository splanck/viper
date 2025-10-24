// File: src/il/verify/VerifierTable.hpp
// Purpose: Declares lookup helpers for opcode verification properties.
// Key invariants: Table entries cover only opcodes with simple arithmetic rules.
// Ownership/Lifetime: Returned data references static storage.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace il::verify
{

/// @brief Classification used by the verifier to describe operand/result kinds.
enum class TypeClass : uint8_t
{
    None,      ///< No constraint or unused slot.
    Void,      ///< Void type constraint.
    I1,        ///< 1-bit integer type.
    I16,       ///< 16-bit integer type.
    I32,       ///< 32-bit integer type.
    I64,       ///< 64-bit integer type.
    F64,       ///< 64-bit floating point type.
    Ptr,       ///< Pointer type.
    Str,       ///< Runtime string handle type.
    Error,     ///< Error object type.
    ResumeTok, ///< Resume token type.
    InstrType  ///< Use the instruction's declared type.
};

/// @brief Verification properties describing a simple arithmetic opcode.
struct OpProps
{
    uint8_t arity;      ///< Number of value operands required.
    TypeClass operands; ///< Shared operand type requirement.
    TypeClass result;   ///< Result type produced on success.
    bool canTrap;       ///< Whether the opcode may trap at runtime.
};

/// @brief Specification of operand and result constraints for opcode verification.
struct OpCheckSpec
{
    uint8_t numOperandsMin; ///< Minimum number of operands accepted.
    uint8_t numOperandsMax; ///< Maximum number of operands accepted; may be variadic.
    std::array<TypeClass, il::core::kMaxOperandCategories>
        operandTypes;    ///< Per-operand type constraints.
    TypeClass result;    ///< Result type constraint; InstrType refers to instruction type.
    bool hasSideEffects; ///< Whether the opcode performs side effects.
};

/// @brief Look up verification properties for supported arithmetic opcodes.
/// @param opcode Opcode to query.
/// @return Populated properties when @p opcode is described by the table; empty otherwise.
std::optional<OpProps> lookup(il::core::Opcode opcode);

/// @brief Look up detailed operand/result constraints for an opcode.
/// @param opcode Opcode to query.
/// @return Populated specification when @p opcode is described by the table; empty otherwise.
std::optional<OpCheckSpec> lookupSpec(il::core::Opcode opcode);

/// @brief Determine whether an opcode performs side effects observable by the verifier.
/// @param opcode Opcode to query.
/// @return True when @p opcode has side effects; false otherwise.
bool hasSideEffects(il::core::Opcode opcode);

/// @brief Determine whether an opcode is pure (side-effect free).
/// @param opcode Opcode to query.
/// @return True when @p opcode has no side effects; false otherwise.
inline bool isPure(il::core::Opcode opcode)
{
    return !hasSideEffects(opcode);
}

} // namespace il::verify
