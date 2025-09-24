// File: src/il/core/Opcode.hpp
// Purpose: Enumerates IL instruction opcodes.
// Key invariants: Enumeration values match IL spec.
// Ownership/Lifetime: Not applicable.
// Links: docs/il-guide.md#reference
#pragma once

#include <cstddef>
#include <string>

namespace il::core
{

/// @brief All instruction opcodes defined by the IL.
/// @see docs/il-guide.md#reference §3 for opcode descriptions.
enum class Opcode
{
#define IL_OPCODE(NAME, ...) NAME,
#include "il/core/Opcode.def"
#undef IL_OPCODE
    Count
};

/// @brief Total number of opcodes defined by the IL.
constexpr size_t kNumOpcodes = static_cast<size_t>(Opcode::Count);

/// @brief Convert opcode @p op to its mnemonic string.
/// @param op Opcode to stringify.
/// @return Lowercase mnemonic defined by the IL spec.
std::string toString(Opcode op);

} // namespace il::core
