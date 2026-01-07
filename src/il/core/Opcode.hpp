//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Opcode enumeration, which defines all instruction
// operation codes supported by Viper IL. The opcode determines the semantics
// of each instruction and drives instruction selection, verification, and
// execution in the VM and native backends.
//
// Opcodes are generated from a canonical definition file (Opcode.def) using
// X-macro techniques. This ensures that opcode metadata (names, categories,
// operand counts) remains synchronized across the parser, verifier, serializer,
// and execution engines.
//
// The IL supports opcodes for:
// - Arithmetic: add, sub, mul, div variants with overflow/trap semantics
// - Comparisons: integer and floating-point relational operators
// - Memory: alloca, load, store, gep for stack/heap access
// - Control flow: br, cbr, ret, switch for CFG construction
// - Calls: call and call.indirect for function invocation
// - Type conversions: casts between integer, float, and pointer types
// - Exception handling: eh.push, eh.pop, trap family for error propagation
// - Bitwise: and, or, xor, shifts for integer manipulation
//
// Design Rationale:
// - Enumeration-based: Simple switch-based dispatch without virtual calls
// - X-macro generation: Single source of truth for opcode definitions
// - Count sentinel: Opcode::Count enables compile-time table sizing
// - String conversion: toString() provides spec-compliant mnemonics
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
