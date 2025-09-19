// File: src/il/core/OpcodeInfo.cpp
// Purpose: Defines metadata describing IL opcode signatures and behaviours.
// Key invariants: Table entries stay in sync with the Opcode enumeration order.
// Ownership/Lifetime: Static storage duration, read-only access via kOpcodeTable.
// Links: docs/il-spec.md

#include "il/core/OpcodeInfo.hpp"

#include <string>

namespace il::core
{

namespace
{
constexpr std::array<TypeCategory, kMaxOperandCategories> makeOperands(TypeCategory a,
                                                                        TypeCategory b = TypeCategory::None,
                                                                        TypeCategory c = TypeCategory::None)
{
    return {a, b, c};
}
} // namespace

const std::array<OpcodeInfo, kNumOpcodes> kOpcodeTable = {
    {
#define IL_OPCODE(NAME, MNEMONIC, RES_ARITY, RES_TYPE, MIN_OPS, MAX_OPS, OP0, OP1, OP2, SIDE_EFFECTS, SUCCESSORS, TERMINATOR,      \
                  DISPATCH)                                                                                                       \
        {MNEMONIC, RES_ARITY, RES_TYPE, MIN_OPS, MAX_OPS, makeOperands(OP0, OP1, OP2), SIDE_EFFECTS, SUCCESSORS, TERMINATOR,      \
         DISPATCH},
#include "il/core/Opcode.def"
#undef IL_OPCODE
    }
};

static_assert(kOpcodeTable.size() == kNumOpcodes, "Opcode table must match enum count");

std::string toString(Opcode op)
{
    const size_t idx = static_cast<size_t>(op);
    if (idx >= kOpcodeTable.size())
        return "";
    return kOpcodeTable[idx].name;
}

} // namespace il::core
