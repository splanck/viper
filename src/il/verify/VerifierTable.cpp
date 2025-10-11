// File: src/il/verify/VerifierTable.cpp
// Purpose: Defines lookup helpers for opcode verification properties.
// Key invariants: Lookup table indices correspond to il::core::Opcode enumerators.
// Ownership/Lifetime: Table has static storage duration.
// Links: docs/il-guide.md#reference

#include "il/verify/VerifierTable.hpp"

#include <array>

namespace il::verify
{
namespace
{
using il::core::Opcode;
using Table = std::array<std::optional<OpProps>, static_cast<size_t>(Opcode::Count)>;

constexpr OpProps makeBinary(TypeClass cls, TypeClass result, bool canTrap)
{
    return OpProps{2, cls, result, canTrap};
}

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

std::optional<OpProps> lookup(Opcode opcode)
{
    const size_t index = static_cast<size_t>(opcode);
    if (index >= kTable.size())
        return std::nullopt;
    return kTable[index];
}

namespace
{

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

