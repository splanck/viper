//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/core/Opcode.hpp
// Purpose: Declares the Opcode enum class -- all instruction operation codes
//          supported by Viper IL. Generated from Opcode.def via X-macros,
//          covering arithmetic, comparisons, memory, control flow, calls,
//          casts, exception handling, and bitwise operations.
// Key invariants:
//   - Opcode::Count is a sentinel past the last valid enumerator.
//   - toString() returns a spec-compliant lowercase mnemonic for every opcode.
//   - Opcode values are contiguous starting from 0.
// Ownership/Lifetime: Enum is a value type with no dynamic resources.
// Links: docs/il-guide.md#reference, il/core/Opcode.def
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

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
const char *toString(Opcode op);

} // namespace il::core
