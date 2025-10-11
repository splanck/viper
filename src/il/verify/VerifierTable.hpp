// File: src/il/verify/VerifierTable.hpp
// Purpose: Declares lookup helpers for opcode verification properties.
// Key invariants: Table entries cover only opcodes with simple arithmetic rules.
// Ownership/Lifetime: Returned data references static storage.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Opcode.hpp"

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
    uint8_t arity;     ///< Number of value operands required.
    TypeClass operands;///< Shared operand type requirement.
    TypeClass result;  ///< Result type produced on success.
    bool canTrap;      ///< Whether the opcode may trap at runtime.
};

/// @brief Look up verification properties for supported arithmetic opcodes.
/// @param opcode Opcode to query.
/// @return Populated properties when @p opcode is described by the table; empty otherwise.
std::optional<OpProps> lookup(il::core::Opcode opcode);

} // namespace il::verify

