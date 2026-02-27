//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Warnings.cpp
/// @brief Warning code tables and policy logic for the Zia compiler.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Warnings.hpp"
#include <cstdio>
#include <cstring>

namespace il::frontends::zia
{

//=============================================================================
// Warning Code/Name Tables
//=============================================================================

/// @brief Entry in the warning info table.
struct WarningInfo
{
    WarningCode code;
    const char *codeStr; ///< e.g., "W001"
    const char *name;    ///< e.g., "unused-variable"
};

/// @brief Static table mapping warning codes to strings and names.
/// @details Indexed by (code - 1). Must be kept in sync with WarningCode enum.
static constexpr WarningInfo kWarningTable[] = {
    {WarningCode::W001_UnusedVariable, "W001", "unused-variable"},
    {WarningCode::W002_UnreachableCode, "W002", "unreachable-code"},
    {WarningCode::W003_ImplicitNarrowing, "W003", "implicit-narrowing"},
    {WarningCode::W004_VariableShadowing, "W004", "variable-shadowing"},
    {WarningCode::W005_FloatEquality, "W005", "float-equality"},
    {WarningCode::W006_EmptyLoopBody, "W006", "empty-loop-body"},
    {WarningCode::W007_AssignmentInCondition, "W007", "assignment-in-condition"},
    {WarningCode::W008_MissingReturn, "W008", "missing-return"},
    {WarningCode::W009_SelfAssignment, "W009", "self-assignment"},
    {WarningCode::W010_DivisionByZero, "W010", "division-by-zero"},
    {WarningCode::W011_RedundantBoolComparison, "W011", "redundant-bool-comparison"},
    {WarningCode::W012_DuplicateImport, "W012", "duplicate-import"},
    {WarningCode::W013_EmptyBody, "W013", "empty-body"},
    {WarningCode::W014_UnusedResult, "W014", "unused-result"},
    {WarningCode::W015_UninitializedVariable, "W015", "uninitialized-variable"},
    {WarningCode::W016_OptionalWithoutCheck, "W016", "optional-without-check"},
    {WarningCode::W017_XorConfusion, "W017", "xor-confusion"},
    {WarningCode::W018_BitwiseAndConfusion, "W018", "bitwise-and-confusion"},
};

static_assert(sizeof(kWarningTable) / sizeof(kWarningTable[0]) == kWarningCodeCount,
              "kWarningTable must have exactly kWarningCodeCount entries");

/// @brief Look up a WarningInfo by code value. Returns nullptr if out of range.
static const WarningInfo *lookupInfo(WarningCode code)
{
    auto idx = static_cast<uint16_t>(code);
    if (idx < 1 || idx > kWarningCodeCount)
        return nullptr;
    return &kWarningTable[idx - 1];
}

const char *warningCodeStr(WarningCode code)
{
    const auto *info = lookupInfo(code);
    return info ? info->codeStr : "W???";
}

const char *warningName(WarningCode code)
{
    const auto *info = lookupInfo(code);
    return info ? info->name : "unknown";
}

std::optional<WarningCode> parseWarningCode(std::string_view name)
{
    // Try matching by code string (e.g., "W001")
    for (const auto &entry : kWarningTable)
    {
        if (name == entry.codeStr)
            return entry.code;
    }

    // Try matching by slug name (e.g., "unused-variable")
    for (const auto &entry : kWarningTable)
    {
        if (name == entry.name)
            return entry.code;
    }

    return std::nullopt;
}

//=============================================================================
// Warning Policy
//=============================================================================

bool WarningPolicy::isEnabled(WarningCode code) const
{
    // Explicitly disabled always wins
    if (disabled.count(code))
        return false;

    // -Wall enables everything
    if (enableAll)
        return true;

    // Otherwise, check default set
    return defaultEnabled().count(code) > 0;
}

const std::unordered_set<WarningCode> &WarningPolicy::defaultEnabled()
{
    // Conservative set — these catch common real bugs without being noisy.
    // W002 (unreachable), W003 (narrowing), W004 (shadowing), W006 (empty loop),
    // W007 (assign-in-cond), W011 (redundant bool), W013 (empty body),
    // W014 (unused result) are -Wall only.
    static const std::unordered_set<WarningCode> defaults = {
        WarningCode::W001_UnusedVariable,
        WarningCode::W005_FloatEquality,
        WarningCode::W008_MissingReturn,
        WarningCode::W009_SelfAssignment,
        WarningCode::W010_DivisionByZero,
        WarningCode::W012_DuplicateImport,
        WarningCode::W015_UninitializedVariable,
        WarningCode::W016_OptionalWithoutCheck,
        WarningCode::W017_XorConfusion,
        WarningCode::W018_BitwiseAndConfusion,
    };
    return defaults;
}

} // namespace il::frontends::zia
