// File: src/frontends/basic/Intrinsics.cpp
// Purpose: Define registry of BASIC intrinsic functions.
// Key invariants: Table contents remain sorted by declaration order.
// Ownership/Lifetime: Intrinsic descriptors are static.
// Links: docs/class-catalog.md

#include "frontends/basic/Intrinsics.hpp"

#include <array>

namespace il::frontends::basic::intrinsics
{
namespace
{
// Common parameter descriptors.
/// Signature: (string)
constexpr Param strParam[] = {{Type::String, false}};
/// Signature: (int)
constexpr Param intParam[] = {{Type::Int, false}};
/// Signature: (numeric)
constexpr Param numParam[] = {{Type::Numeric, false}};

/// Signature: (string, int)
constexpr Param leftRightParams[] = {
    {Type::String, false},
    {Type::Int, false},
};

/// Signature: (string, int, [int])
constexpr Param midParams[] = {
    {Type::String, false},
    {Type::Int, false},
    {Type::Int, true},
};

/// Signature: ([int], string, string)
constexpr Param instrParams[] = {
    {Type::Int, true},
    {Type::String, false},
    {Type::String, false},
};

/// Registry mapping intrinsic names to return types and parameter signatures.
constexpr Intrinsic table[] = {
    {"LEFT$", Type::String, leftRightParams, std::size(leftRightParams)},
    {"RIGHT$", Type::String, leftRightParams, std::size(leftRightParams)},
    {"MID$", Type::String, midParams, std::size(midParams)},
    {"INSTR", Type::Int, instrParams, std::size(instrParams)},
    {"LEN", Type::Int, strParam, std::size(strParam)},
    {"LTRIM$", Type::String, strParam, std::size(strParam)},
    {"RTRIM$", Type::String, strParam, std::size(strParam)},
    {"TRIM$", Type::String, strParam, std::size(strParam)},
    {"UCASE$", Type::String, strParam, std::size(strParam)},
    {"LCASE$", Type::String, strParam, std::size(strParam)},
    {"CHR$", Type::String, intParam, std::size(intParam)},
    {"ASC", Type::Int, strParam, std::size(strParam)},
    {"VAL", Type::Numeric, strParam, std::size(strParam)},
    {"STR$", Type::String, numParam, std::size(numParam)},
};
} // namespace

const Intrinsic *lookup(std::string_view name)
{
    for (const auto &intr : table)
        if (intr.name == name)
            return &intr;
    return nullptr;
}

void dumpNames(std::ostream &os)
{
    for (std::size_t i = 0; i < std::size(table); ++i)
    {
        os << table[i].name;
        if (i + 1 < std::size(table))
            os << ' ';
    }
}

} // namespace il::frontends::basic::intrinsics
