// File: src/il/core/Opcode.hpp
// Purpose: Enumerates IL instruction opcodes.
// Key invariants: Enumeration values match IL spec.
// Ownership/Lifetime: Not applicable.
// Links: docs/il-spec.md
#pragma once

#include <cstddef>
#include <string>

namespace il::core
{

/// @brief All instruction opcodes defined by the IL.
/// @see docs/il-spec.md §3 for opcode descriptions.
enum class Opcode
{
    Add,
    Sub,
    Mul,
    SDiv,
    UDiv,
    SRem,
    URem,
    And,
    Or,
    Xor,
    Shl,
    LShr,
    AShr,
    FAdd,
    FSub,
    FMul,
    FDiv,
    ICmpEq,
    ICmpNe,
    SCmpLT,
    SCmpLE,
    SCmpGT,
    SCmpGE,
    UCmpLT,
    UCmpLE,
    UCmpGT,
    UCmpGE,
    FCmpEQ,
    FCmpNE,
    FCmpLT,
    FCmpLE,
    FCmpGT,
    FCmpGE,
    Sitofp,
    Fptosi,
    Zext1,
    Trunc1,
    Alloca,
    GEP,
    Load,
    Store,
    AddrOf,
    ConstStr,
    ConstNull,
    Call,
    Br,
    CBr,
    Ret,
    Trap
};

/// @brief Total number of opcodes defined by the IL.
constexpr size_t kNumOpcodes = static_cast<size_t>(Opcode::Trap) + 1;

/// @brief Convert opcode @p op to its mnemonic string.
/// @param op Opcode to stringify.
/// @return Lowercase mnemonic defined by the IL spec.
std::string toString(Opcode op);

} // namespace il::core
