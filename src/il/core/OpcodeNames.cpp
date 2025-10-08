/**
 * @file OpcodeNames.cpp
 * @brief Provides lightweight mnemonic lookups for IL opcodes.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     The file defines a constexpr lookup table generated from `Opcode.def` and
 *     exposes `toString` for translating opcodes into mnemonic strings.
 */

#include "il/core/Opcode.hpp"

#include <array>

namespace il::core
{
namespace
{
/**
 * @brief Compile-time array of opcode mnemonics generated from `Opcode.def`.
 *
 * The array order mirrors the `Opcode` enumeration to allow O(1) translation
 * from enumeration value to mnemonic.  A static assertion verifies the sizes
 * remain in sync.
 */
constexpr std::array<const char *, kNumOpcodes> kOpcodeNames = {
#define IL_OPCODE(NAME, MNEMONIC, ...) MNEMONIC,
#include "il/core/Opcode.def"
#undef IL_OPCODE
};

static_assert(kOpcodeNames.size() == kNumOpcodes, "Opcode name table must match enum count");
} // namespace

/**
 * @brief Converts an opcode into its mnemonic string representation.
 *
 * The function performs a bounds check to guard against invalid enumeration
 * values.  Out-of-range inputs yield an empty string literal.
 *
 * @param op Opcode to translate.
 * @return Pointer to a null-terminated mnemonic string; empty string when invalid.
 */
const char *toString(Opcode op)
{
    const size_t index = static_cast<size_t>(op);
    if (index < kOpcodeNames.size())
        return kOpcodeNames[index];
    return "";
}

} // namespace il::core
