// File: src/il/core/OpcodeInfo.cpp
// Purpose: Defines metadata describing IL opcode signatures and behaviours.
// Key invariants: Table entries stay in sync with the Opcode enumeration order.
// Ownership/Lifetime: Static storage duration, read-only access via kOpcodeTable.
// License: MIT
// Links: docs/il-spec.md

#include "il/core/OpcodeInfo.hpp"

#include <string>

namespace il::core
{

namespace
{
/// Builds the operand category array for an opcode, defaulting unspecified entries to None.
constexpr std::array<TypeCategory, kMaxOperandCategories> makeOperands(TypeCategory a,
                                                                        TypeCategory b = TypeCategory::None,
                                                                        TypeCategory c = TypeCategory::None)
{
    return {a, b, c};
}

/// Builds a parser descriptor for a single operand slot.
constexpr OperandParseSpec makeParseSpec(OperandParseKind kind = OperandParseKind::None,
                                         const char *role = nullptr)
{
    return {kind, role};
}

/// Builds the operand-parse descriptor array for an opcode definition.
constexpr std::array<OperandParseSpec, kMaxOperandParseEntries>
makeParseList(OperandParseSpec a = makeParseSpec(),
              OperandParseSpec b = makeParseSpec(),
              OperandParseSpec c = makeParseSpec(),
              OperandParseSpec d = makeParseSpec())
{
    return {a, b, c, d};
}
} // namespace

const std::array<OpcodeInfo, kNumOpcodes> kOpcodeTable = {
    {
#define IL_OPCODE(NAME, MNEMONIC, RES_ARITY, RES_TYPE, MIN_OPS, MAX_OPS, OP0, OP1, OP2, SIDE_EFFECTS, SUCCESSORS, TERMINATOR,     \
                  DISPATCH, PARSE0, PARSE1, PARSE2, PARSE3)                                                                        \
        {MNEMONIC, RES_ARITY, RES_TYPE, MIN_OPS, MAX_OPS, makeOperands(OP0, OP1, OP2), SIDE_EFFECTS, SUCCESSORS, TERMINATOR,      \
         DISPATCH, makeParseList(PARSE0, PARSE1, PARSE2, PARSE3)},
#include "il/core/Opcode.def"
#undef IL_OPCODE
    }
};

static_assert(kOpcodeTable.size() == kNumOpcodes, "Opcode table must match enum count");

const OpcodeInfo &getOpcodeInfo(Opcode op)
{
    return kOpcodeTable[static_cast<size_t>(op)];
}

bool isVariadicOperandCount(uint8_t value)
{
    return value == kVariadicOperandCount;
}

/**
 * @brief Returns the mnemonic associated with the provided opcode.
 *
 * Performs a direct lookup into the opcode metadata table and treats any opcode whose
 * numerical value falls outside the table bounds as invalid.
 *
 * @param op Opcode enumeration value to translate into a mnemonic string.
 * @returns The mnemonic string if the opcode is within range; otherwise an empty string.
 */
std::string toString(Opcode op)
{
    const size_t idx = static_cast<size_t>(op);
    if (idx >= kOpcodeTable.size())
        return "";
    return kOpcodeTable[idx].name;
}

} // namespace il::core
