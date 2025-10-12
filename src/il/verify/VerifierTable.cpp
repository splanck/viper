//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Defines the static lookup tables that describe opcode verification
// properties.  The tables capture operand arity, operand categories, and
// whether the instruction may trap so the verifier can enforce the rules
// codified in the IL specification without reparsing metadata at runtime.
//
//===----------------------------------------------------------------------===//

#include "il/verify/VerifierTable.hpp"

#include <array>

namespace il::verify
{
namespace
{
using il::core::Opcode;
using Table = std::array<std::optional<OpProps>, static_cast<size_t>(Opcode::Count)>;

/// @brief Helper to build the property entry for two-operand instructions.
///
/// Encodes a consistent operand count and operand/result class set for opcodes
/// whose behaviour is defined entirely by the operand type and an optional trap
/// bit.
constexpr OpProps makeBinary(TypeClass cls, TypeClass result, bool canTrap)
{
    return OpProps{2, cls, result, canTrap};
}

/// @brief Populate the opcode property table at compile time.
///
/// The constexpr builder initialises the sparse array, filling entries only for
/// the opcodes that currently expose additional verification metadata.  Missing
/// entries remain `std::nullopt`, signalling that the verifier should consult
/// the generic opcode information instead.
constexpr Table buildTable()
{
    Table table{};
    table.fill(std::nullopt);

    table[static_cast<size_t>(Opcode::IAddOvf)] = makeBinary(TypeClass::I64, TypeClass::I64, true);
    table[static_cast<size_t>(Opcode::ISubOvf)] = makeBinary(TypeClass::I64, TypeClass::I64, true);
    table[static_cast<size_t>(Opcode::IMulOvf)] = makeBinary(TypeClass::I64, TypeClass::I64, true);
    table[static_cast<size_t>(Opcode::SDivChk0)] = makeBinary(TypeClass::I64, TypeClass::I64, true);
    table[static_cast<size_t>(Opcode::FAdd)] = makeBinary(TypeClass::F64, TypeClass::F64, false);
    table[static_cast<size_t>(Opcode::FSub)] = makeBinary(TypeClass::F64, TypeClass::F64, false);
    table[static_cast<size_t>(Opcode::FMul)] = makeBinary(TypeClass::F64, TypeClass::F64, false);
    table[static_cast<size_t>(Opcode::FDiv)] = makeBinary(TypeClass::F64, TypeClass::F64, false);

    return table;
}

const Table kTable = buildTable();

} // namespace

/// @brief Retrieve the property record for a specific opcode.
///
/// Performs a bounds check before indexing into the static property table so
/// callers never read past the end when presented with an invalid opcode.
///
/// @param opcode Opcode to inspect.
/// @return Additional verification properties or `std::nullopt` if none exist.
std::optional<OpProps> lookup(Opcode opcode)
{
    const size_t index = static_cast<size_t>(opcode);
    if (index >= kTable.size())
        return std::nullopt;
    return kTable[index];
}

namespace
{

/// @brief Translate IL type categories into verifier type classes.
///
/// The verifier groups IL types into broader equivalence classes to simplify
/// operand validation.  This helper implements the mapping while providing a
/// default of TypeClass::None for categories without explicit verifier support.
constexpr TypeClass mapCategory(il::core::TypeCategory category)
{
    using il::core::TypeCategory;
    switch (category)
    {
    case TypeCategory::I1:
        return TypeClass::I1;
    case TypeCategory::I16:
        return TypeClass::I16;
    case TypeCategory::I32:
        return TypeClass::I32;
    case TypeCategory::I64:
        return TypeClass::I64;
    case TypeCategory::F64:
        return TypeClass::F64;
    case TypeCategory::Ptr:
        return TypeClass::Ptr;
    case TypeCategory::Str:
        return TypeClass::Str;
    case TypeCategory::Error:
        return TypeClass::Error;
    case TypeCategory::ResumeTok:
        return TypeClass::ResumeTok;
    case TypeCategory::InstrType:
        return TypeClass::InstrType;
    case TypeCategory::Void:
        return TypeClass::Void;
    case TypeCategory::None:
    case TypeCategory::Any:
    case TypeCategory::Dynamic:
        return TypeClass::None;
    default:
        return TypeClass::None;
    }
}

} // namespace

/// @brief Expand the opcode metadata into the verifier's check specification.
///
/// Copies operand and result category ranges from the opcode metadata table
/// while translating IL type categories into the verifier's classification
/// scheme.  Callers receive a populated specification only when the opcode is
/// valid; invalid opcodes yield `std::nullopt` so the caller can emit its own
/// diagnostic.
std::optional<OpCheckSpec> lookupSpec(Opcode opcode)
{
    const size_t index = static_cast<size_t>(opcode);
    if (index >= il::core::kNumOpcodes)
        return std::nullopt;

    const auto &info = il::core::getOpcodeInfo(opcode);

    OpCheckSpec spec{};
    spec.numOperandsMin = info.numOperandsMin;
    spec.numOperandsMax = info.numOperandsMax;
    for (size_t i = 0; i < il::core::kMaxOperandCategories; ++i)
        spec.operandTypes[i] = mapCategory(info.operandTypes[i]);
    spec.result = mapCategory(info.resultType);
    spec.hasSideEffects = info.hasSideEffects;

    return spec;
}

} // namespace il::verify

