/**
 * @file OpcodeInfo.cpp
 * @brief Defines metadata describing IL opcode signatures and behaviour.
 * @copyright
 *     MIT License. See the LICENSE file in the project root for full terms.
 * @details
 *     The opcode table is generated from `Opcode.def` and exposes helpers for
 *     querying operand categories, parse specifications, and mnemonic strings.
 */

#include "il/core/OpcodeInfo.hpp"

#include <string>
#include <vector>

namespace il::core
{

namespace
{
/**
 * @brief Builds the operand category descriptor array for an opcode.
 *
 * The helper fills the fixed-size operand category array used by `OpcodeInfo`.
 * Unspecified entries default to `TypeCategory::None`, ensuring the table has
 * a consistent size regardless of the opcode's arity.
 *
 * @param a Category for the first operand slot.
 * @param b Category for the second operand slot (optional).
 * @param c Category for the third operand slot (optional).
 * @return Array populated with up to three operand categories.
 */
constexpr std::array<TypeCategory, kMaxOperandCategories> makeOperands(TypeCategory a,
                                                                        TypeCategory b = TypeCategory::None,
                                                                        TypeCategory c = TypeCategory::None)
{
    return {a, b, c};
}

/**
 * @brief Creates a parse specification entry for an operand.
 *
 * Parse specifications describe how textual operands should be interpreted when
 * parsing IL.  Callers typically use the defaults, which correspond to "no
 * operand".
 *
 * @param kind Kind of operand expected at the position.
 * @param role Optional mnemonic describing the operand's role.
 * @return Parse specification structure for an operand slot.
 */
constexpr OperandParseSpec makeParseSpec(OperandParseKind kind = OperandParseKind::None,
                                         const char *role = nullptr)
{
    return {kind, role};
}

/**
 * @brief Aggregates up to four operand parse specifications into an array.
 *
 * The helper normalizes missing operands by using the default `makeParseSpec`
 * values, producing a consistent array size for every opcode definition.
 *
 * @param a Parse specification for operand 0.
 * @param b Parse specification for operand 1.
 * @param c Parse specification for operand 2.
 * @param d Parse specification for operand 3.
 * @return Array containing the provided parse specifications.
 */
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

/**
 * @brief Retrieves the metadata descriptor for a particular opcode.
 *
 * The function indexes the generated table using the opcode's underlying
 * numeric value.  Callers must ensure the opcode is within range, as the table
 * lookup does not perform bounds checking in release builds.
 *
 * @param op Opcode whose metadata should be returned.
 * @return Reference to the static `OpcodeInfo` descriptor.
 */
const OpcodeInfo &getOpcodeInfo(Opcode op)
{
    return kOpcodeTable[static_cast<size_t>(op)];
}

/**
 * @brief Enumerates every opcode defined in the IL.
 *
 * The helper materializes a vector of opcodes in declaration order, which can
 * be used for iteration in tools or validation passes.
 *
 * @return Vector containing each opcode exactly once.
 */
std::vector<Opcode> all_opcodes()
{
    std::vector<Opcode> ops;
    ops.reserve(kNumOpcodes);
    for (size_t index = 0; index < kNumOpcodes; ++index)
        ops.push_back(static_cast<Opcode>(index));
    return ops;
}

/**
 * @brief Tests whether a count marker indicates a variadic operand list.
 *
 * Some opcodes use the sentinel value `kVariadicOperandCount` to signal that the
 * operand count is not fixed.  This helper centralizes the comparison.
 *
 * @param value Operand count marker to inspect.
 * @return `true` when @p value denotes a variadic count.
 */
bool isVariadicOperandCount(uint8_t value)
{
    return value == kVariadicOperandCount;
}

/**
 * @brief Tests whether a count marker indicates a variadic successor list.
 *
 * Control-flow successors reuse the same sentinel as operand counts.  This
 * helper keeps the logic in one place for clarity.
 *
 * @param value Successor count marker to inspect.
 * @return `true` when @p value denotes a variadic count.
 */
bool isVariadicSuccessorCount(uint8_t value)
{
    return value == kVariadicOperandCount;
}

/**
 * @brief Returns the mnemonic associated with the provided opcode.
 *
 * Delegates to the generated opcode name table and treats out-of-range opcodes
 * as invalid.
 *
 * @param op Opcode enumeration value to translate into a mnemonic string.
 * @returns The mnemonic string if the opcode is within range; otherwise an empty string.
 */
std::string opcode_mnemonic(Opcode op)
{
    return toString(op);
}

} // namespace il::core
