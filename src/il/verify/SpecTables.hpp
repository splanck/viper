// File: src/il/verify/SpecTables.hpp
// Purpose: Declares instruction specification tables derived from the opcode schema.
// Key invariants: Every opcode defined in il::core::Opcode has a corresponding entry.
// Ownership/Lifetime: Table data has static storage duration and is read-only.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"

#include <array>

namespace il::verify::spec
{

/// @brief Result specification extracted from the opcode schema.
struct ResultSpec
{
    il::core::ResultArity arity;      ///< Required result arity.
    il::core::TypeCategory type;     ///< Result type category; InstrType uses the instruction annotation.
};

/// @brief Operand specification describing arity and per-slot categories.
struct OperandSpec
{
    uint8_t min; ///< Minimum operand count.
    uint8_t max; ///< Maximum operand count (variadic uses kVariadicOperandCount).
    std::array<il::core::TypeCategory, il::core::kMaxOperandCategories>
        types; ///< Operand type categories.
};

/// @brief Flag specification covering side effects and control-flow metadata.
struct FlagSpec
{
    bool hasSideEffects; ///< Whether the instruction may observe side effects.
    uint8_t successors;  ///< Number of required successor labels (variadic allowed).
    bool terminator;     ///< Whether the instruction terminates a basic block.
};

/// @brief Full per-opcode specification consumed by the verifier.
struct InstructionSpec
{
    il::core::Opcode opcode; ///< Opcode enumerator.
    const char *mnemonic;    ///< Canonical mnemonic string.
    ResultSpec result;       ///< Result constraints.
    OperandSpec operands;    ///< Operand constraints.
    FlagSpec flags;          ///< Side-effect and control-flow metadata.
};

/// @brief Retrieve the specification entry for @p opcode.
/// @param opcode Opcode to query.
/// @return Reference to the statically stored specification.
const InstructionSpec &lookup(il::core::Opcode opcode);

/// @brief Access the entire specification table.
/// @return Reference to the static table indexed by opcode.
const std::array<InstructionSpec, il::core::kNumOpcodes> &all();

} // namespace il::verify::spec

