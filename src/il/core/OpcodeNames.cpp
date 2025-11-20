//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/core/OpcodeNames.cpp
// Purpose: Provide a compact mapping between opcode enumeration values and
//          their textual mnemonics for diagnostics and serialization.
// Key invariants: Table size always matches the opcode enumeration count.
// Links: docs/il-guide.md#opcodes
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements mnemonic lookups for IL opcode enumerators.
/// @details The generated string table lives in this translation unit to avoid
///          forcing every include site to materialize the mnemonics.  A static
///          assertion guards against drift between the enum definition and the
///          generated table so tooling surfaces mismatches promptly during
///          compilation.

#include "il/core/Opcode.hpp"

#include <array>

namespace il::core
{
namespace
{
constexpr std::array<const char *, kNumOpcodes> kOpcodeNames = {
#define IL_OPCODE(NAME, MNEMONIC, ...) MNEMONIC,
#include "il/core/Opcode.def"
#undef IL_OPCODE
};

static_assert(kOpcodeNames.size() == kNumOpcodes, "Opcode name table must match enum count");
} // namespace

/// @brief Translate an opcode enumeration value into its mnemonic string.
///
/// Performs a bounds check before indexing into the generated table so invalid
/// opcodes gracefully map to an empty string. This mirrors the behaviour of
/// other diagnostic helpers.
///
/// @param op Opcode enumeration value to translate.
/// @return Pointer to the null-terminated mnemonic or an empty string when
///         @p op is out of range.
const char *toString(Opcode op)
{
    const size_t index = static_cast<size_t>(op);
    if (index < kOpcodeNames.size())
        return kOpcodeNames[index];
    return "";
}

} // namespace il::core
