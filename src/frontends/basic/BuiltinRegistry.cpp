// File: src/frontends/basic/BuiltinRegistry.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements registry of BASIC built-ins for semantic analysis and
//          lowering dispatch.
// Key invariants: Registry entries correspond 1:1 with BuiltinCallExpr::Builtin
//                 enum order.
// Ownership/Lifetime: Static data only.
// Links: docs/class-catalog.md

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include <array>
#include <unordered_map>

namespace il::frontends::basic
{
namespace
{
using B = BuiltinCallExpr::Builtin;

// Dense metadata table indexed by BuiltinCallExpr::Builtin enumerators. The
// order must remain in lockstep with the enum definition so that we can index
// directly without translation.
static const std::array<BuiltinInfo, 23> kBuiltins = {{
    {"LEN", 1, 1, &SemanticAnalyzer::analyzeLen, &Lowerer::lowerLen, &Lowerer::scanLen},
    {"MID$", 2, 3, &SemanticAnalyzer::analyzeMid, &Lowerer::lowerMid, &Lowerer::scanMid},
    {"LEFT$", 2, 2, &SemanticAnalyzer::analyzeLeft, &Lowerer::lowerLeft, &Lowerer::scanLeft},
    {"RIGHT$", 2, 2, &SemanticAnalyzer::analyzeRight, &Lowerer::lowerRight, &Lowerer::scanRight},
    {"STR$", 1, 1, &SemanticAnalyzer::analyzeStr, &Lowerer::lowerStr, &Lowerer::scanStr},
    {"VAL", 1, 1, &SemanticAnalyzer::analyzeVal, &Lowerer::lowerVal, &Lowerer::scanVal},
    {"INT", 1, 1, &SemanticAnalyzer::analyzeInt, &Lowerer::lowerInt, &Lowerer::scanInt},
    {"SQR", 1, 1, &SemanticAnalyzer::analyzeSqr, &Lowerer::lowerSqr, &Lowerer::scanSqr},
    {"ABS", 1, 1, &SemanticAnalyzer::analyzeAbs, &Lowerer::lowerAbs, &Lowerer::scanAbs},
    {"FLOOR", 1, 1, &SemanticAnalyzer::analyzeFloor, &Lowerer::lowerFloor, &Lowerer::scanFloor},
    {"CEIL", 1, 1, &SemanticAnalyzer::analyzeCeil, &Lowerer::lowerCeil, &Lowerer::scanCeil},
    {"SIN", 1, 1, &SemanticAnalyzer::analyzeSin, &Lowerer::lowerSin, &Lowerer::scanSin},
    {"COS", 1, 1, &SemanticAnalyzer::analyzeCos, &Lowerer::lowerCos, &Lowerer::scanCos},
    {"POW", 2, 2, &SemanticAnalyzer::analyzePow, &Lowerer::lowerPow, &Lowerer::scanPow},
    {"RND", 0, 0, &SemanticAnalyzer::analyzeRnd, &Lowerer::lowerRnd, &Lowerer::scanRnd},
    {"INSTR", 2, 3, &SemanticAnalyzer::analyzeInstr, &Lowerer::lowerInstr, &Lowerer::scanInstr},
    {"LTRIM$", 1, 1, &SemanticAnalyzer::analyzeLtrim, &Lowerer::lowerLtrim, &Lowerer::scanLtrim},
    {"RTRIM$", 1, 1, &SemanticAnalyzer::analyzeRtrim, &Lowerer::lowerRtrim, &Lowerer::scanRtrim},
    {"TRIM$", 1, 1, &SemanticAnalyzer::analyzeTrim, &Lowerer::lowerTrim, &Lowerer::scanTrim},
    {"UCASE$", 1, 1, &SemanticAnalyzer::analyzeUcase, &Lowerer::lowerUcase, &Lowerer::scanUcase},
    {"LCASE$", 1, 1, &SemanticAnalyzer::analyzeLcase, &Lowerer::lowerLcase, &Lowerer::scanLcase},
    {"CHR$", 1, 1, &SemanticAnalyzer::analyzeChr, &Lowerer::lowerChr, &Lowerer::scanChr},
    {"ASC", 1, 1, &SemanticAnalyzer::analyzeAsc, &Lowerer::lowerAsc, &Lowerer::scanAsc},
}};

// Maps canonical BASIC source spellings to enum entries. Names must already be
// normalized to uppercase with any suffix characters (e.g., $) preserved.
static const std::unordered_map<std::string_view, B> kByName = {
    {"LEN", B::Len},      {"MID$", B::Mid},     {"LEFT$", B::Left}, {"RIGHT$", B::Right},
    {"STR$", B::Str},     {"VAL", B::Val},      {"INT", B::Int},    {"SQR", B::Sqr},
    {"ABS", B::Abs},      {"FLOOR", B::Floor},  {"CEIL", B::Ceil},  {"SIN", B::Sin},
    {"COS", B::Cos},      {"POW", B::Pow},      {"RND", B::Rnd},    {"INSTR", B::Instr},
    {"LTRIM$", B::Ltrim}, {"RTRIM$", B::Rtrim}, {"TRIM$", B::Trim}, {"UCASE$", B::Ucase},
    {"LCASE$", B::Lcase}, {"CHR$", B::Chr},     {"ASC", B::Asc},
};
} // namespace

/// @brief Fetch metadata for a BASIC built-in represented by its enum value.
/// @param b Builtin enumerator to inspect.
/// @return Reference to the metadata describing semantic, lowering, and scan
///         hooks.
/// @pre @p b must be a valid BuiltinCallExpr::Builtin enumerator; passing an
///      out-of-range value results in undefined behavior due to direct array
///      indexing.
const BuiltinInfo &getBuiltinInfo(BuiltinCallExpr::Builtin b)
{
    return kBuiltins[static_cast<std::size_t>(b)];
}

/// @brief Resolve a BASIC built-in enum from its source spelling.
/// @param name Canonical BASIC keyword spelling to look up. The string is
///        matched exactly; callers must provide the normalized uppercase form
///        including any suffix markers.
/// @return Built-in enumerator on success; std::nullopt when the name is not
///         registered.
std::optional<BuiltinCallExpr::Builtin> lookupBuiltin(std::string_view name)
{
    auto it = kByName.find(name);
    if (it == kByName.end())
        return std::nullopt;
    return it->second;
}

} // namespace il::frontends::basic
