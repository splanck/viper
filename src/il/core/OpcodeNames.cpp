// File: src/il/core/OpcodeNames.cpp
// Purpose: Provide lightweight mnemonic lookups for IL opcodes.
// Key invariants: Table entries align exactly with the order of il::core::Opcode.
// Ownership/Lifetime: Static storage duration for shared string literals.
// Links: docs/il-guide.md#reference

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

const char *toString(Opcode op)
{
    const size_t index = static_cast<size_t>(op);
    if (index < kOpcodeNames.size())
        return kOpcodeNames[index];
    return "";
}

} // namespace il::core
