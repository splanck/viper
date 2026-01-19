//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/core/OpcodeInfo.cpp
// Purpose: Define metadata tables describing IL opcode signatures and helper
//          routines for querying them.
// Links: docs/il-guide.md#opcodes
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements helpers that expose opcode metadata to the rest of the IL stack.
/// @details Houses generated tables and lightweight query wrappers so that tools
///          can retrieve operand counts, parse specifications, and mnemonics from
///          a single source of truth.

#include "il/core/OpcodeInfo.hpp"

#include <string>
#include <vector>

namespace il::core
{

namespace
{
/// @brief Build the operand category array for an opcode definition.
/// @details Provides a constexpr helper so the generated table entries stay
///          concise and default unspecified slots to @ref TypeCategory::None.
/// @param a First operand category.
/// @param b Second operand category (optional).
/// @param c Third operand category (optional).
/// @return Array describing up to three operand categories.
constexpr std::array<TypeCategory, kMaxOperandCategories> makeOperands(
    TypeCategory a, TypeCategory b = TypeCategory::None, TypeCategory c = TypeCategory::None)
{
    return {a, b, c};
}

/// @brief Build a parser descriptor for a single operand slot.
/// @details Captures both the parse kind and an optional role string describing
///          the operand to diagnostics so that opcode metadata remains compact
///          yet expressive.
/// @param kind Operand parsing strategy.
/// @param role Optional semantic role name.
/// @return Operand parse descriptor.
constexpr OperandParseSpec makeParseSpec(OperandParseKind kind = OperandParseKind::None,
                                         const char *role = nullptr)
{
    return {kind, role};
}

/// @brief Build the operand-parse descriptor array for an opcode definition.
/// @details Accepts up to four operand parse specs and defaults the remainder so
///          the table entries remain terse while still covering the maximum
///          operand count supported by the IL.
/// @param a First operand parse descriptor.
/// @param b Second operand parse descriptor.
/// @param c Third operand parse descriptor.
/// @param d Fourth operand parse descriptor.
/// @return Array describing parse behaviour for up to four operands.
constexpr std::array<OperandParseSpec, kMaxOperandParseEntries> makeParseList(
    OperandParseSpec a = makeParseSpec(),
    OperandParseSpec b = makeParseSpec(),
    OperandParseSpec c = makeParseSpec(),
    OperandParseSpec d = makeParseSpec())
{
    return {a, b, c, d};
}
} // namespace

const std::array<OpcodeInfo, kNumOpcodes> kOpcodeTable = {{
#define IL_OPCODE(NAME,                                                                            \
                  MNEMONIC,                                                                        \
                  RES_ARITY,                                                                       \
                  RES_TYPE,                                                                        \
                  MIN_OPS,                                                                         \
                  MAX_OPS,                                                                         \
                  OP0,                                                                             \
                  OP1,                                                                             \
                  OP2,                                                                             \
                  SIDE_EFFECTS,                                                                    \
                  SUCCESSORS,                                                                      \
                  TERMINATOR,                                                                      \
                  DISPATCH,                                                                        \
                  PARSE0,                                                                          \
                  PARSE1,                                                                          \
                  PARSE2,                                                                          \
                  PARSE3)                                                                          \
    {MNEMONIC,                                                                                     \
     RES_ARITY,                                                                                    \
     RES_TYPE,                                                                                     \
     MIN_OPS,                                                                                      \
     MAX_OPS,                                                                                      \
     makeOperands(OP0, OP1, OP2),                                                                  \
     SIDE_EFFECTS,                                                                                 \
     SUCCESSORS,                                                                                   \
     TERMINATOR,                                                                                   \
     DISPATCH,                                                                                     \
     makeParseList(PARSE0, PARSE1, PARSE2, PARSE3)},
#include "il/core/Opcode.def"
#undef IL_OPCODE
}};

static_assert(kOpcodeTable.size() == kNumOpcodes, "Opcode table must match enum count");

/// @brief Retrieve the metadata describing a specific opcode.
/// @details Performs a direct index into the generated opcode table and returns
///          a reference so callers can read fields without copying.  The table
///          size is guarded by a static assertion to catch enum/table drift.
/// @param op Opcode whose metadata is required.
/// @return Reference to the immutable opcode descriptor.
const OpcodeInfo &getOpcodeInfo(Opcode op)
{
    return kOpcodeTable[static_cast<size_t>(op)];
}

/// @brief Enumerate every opcode in declaration order.
/// @details Returns a reference to a statically cached vector containing each
///          opcode enumeration value, avoiding allocation on repeated calls.
///          Primarily used by tools that need to iterate over all opcodes in a
///          stable order for reporting or analysis.
/// @return Const reference to vector populated with every opcode value.
const std::vector<Opcode> &all_opcodes()
{
    static const std::vector<Opcode> ops = []() {
        std::vector<Opcode> v;
        v.reserve(kNumOpcodes);
        for (size_t index = 0; index < kNumOpcodes; ++index)
            v.push_back(static_cast<Opcode>(index));
        return v;
    }();
    return ops;
}

/// @brief Provide a conservative memory classification for @p op.
/// @details Primarily used by optimisation and analysis passes to quickly
///          determine whether an instruction interacts with memory.  Only
///          opcodes with well-understood semantics are marked as memory-free;
///          everything else defaults to @ref MemoryEffects::Unknown.
/// @param op Opcode being classified.
/// @return Memory effect classification for @p op.
MemoryEffects memoryEffects(Opcode op) noexcept
{
    switch (op)
    {
        case Opcode::Load:
            return MemoryEffects::Read;
        case Opcode::Store:
            return MemoryEffects::Write;
        case Opcode::Call:
            return MemoryEffects::ReadWrite;

        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::IAddOvf:
        case Opcode::ISubOvf:
        case Opcode::IMulOvf:
        case Opcode::SDiv:
        case Opcode::UDiv:
        case Opcode::SRem:
        case Opcode::URem:
        case Opcode::SDivChk0:
        case Opcode::UDivChk0:
        case Opcode::SRemChk0:
        case Opcode::URemChk0:
        case Opcode::IdxChk:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
        case Opcode::ICmpEq:
        case Opcode::ICmpNe:
        case Opcode::SCmpLT:
        case Opcode::SCmpLE:
        case Opcode::SCmpGT:
        case Opcode::SCmpGE:
        case Opcode::UCmpLT:
        case Opcode::UCmpLE:
        case Opcode::UCmpGT:
        case Opcode::UCmpGE:
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpGT:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGE:
        case Opcode::Sitofp:
        case Opcode::Fptosi:
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk:
        case Opcode::CastSiToFp:
        case Opcode::CastUiToFp:
        case Opcode::Zext1:
        case Opcode::Trunc1:
        case Opcode::GEP:
        case Opcode::AddrOf:
        case Opcode::ConstStr:
        case Opcode::GAddr:
        case Opcode::ConstNull:
        case Opcode::Br:
        case Opcode::CBr:
        case Opcode::SwitchI32:
        case Opcode::Ret:
            return MemoryEffects::None;

        case Opcode::Alloca:
            return MemoryEffects::Write;

        default:
            return MemoryEffects::Unknown;
    }
}

/// @brief Check whether an operand count field encodes the variadic sentinel.
/// @details Metadata tables use a sentinel value to represent "variadic"
///          operand counts.  This helper hides the comparison so callers remain
///          agnostic of the exact encoding.
/// @param value Encoded operand count.
/// @return True when @p value represents a variadic operand count.
bool isVariadicOperandCount(uint8_t value)
{
    return value == kVariadicOperandCount;
}

/// @brief Check whether a successor count field encodes the variadic sentinel.
/// @details Mirrors @ref isVariadicOperandCount for successor metadata so tools
///          can detect branch fan-out encoded as variadic.
/// @param value Encoded successor count.
/// @return True when @p value represents a variadic successor count.
bool isVariadicSuccessorCount(uint8_t value)
{
    return value == kVariadicOperandCount;
}

/// @brief Return the mnemonic associated with the provided opcode.
/// @details Delegates to the generated opcode name table and treats out-of-range
///          opcodes as invalid by returning an empty string.  Consumers typically
///          use this for diagnostics and serialization.
/// @param op Opcode enumeration value to translate into a mnemonic string.
/// @return Mnemonic string if the opcode is within range; otherwise an empty string.
std::string opcode_mnemonic(Opcode op)
{
    return toString(op);
}

} // namespace il::core
