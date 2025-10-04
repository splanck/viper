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

} // namespace il::verify

