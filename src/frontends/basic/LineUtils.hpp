// File: src/frontends/basic/LineUtils.hpp
// Purpose: Provide helpers for reasoning about BASIC line labels.
// Key invariants: Treats non-positive integers as synthetic/unlabeled statements.
// Ownership/Lifetime: Header-only utility with no state.
// Links: docs/codemap.md
#pragma once

#include <type_traits>

namespace il::frontends::basic
{
/// @brief Determine whether a BASIC line label originates from user input.
/// @details BASIC statements parsed without an explicit numeric label are
///          assigned non-positive synthetic identifiers. This helper normalises
///          checks for such cases so callers do not rely on magic sentinels.
/// @tparam T Integral type representing the stored line number.
/// @param line Candidate line label.
/// @return True when no user-provided line label was supplied.
template <typename T> [[nodiscard]] constexpr bool isUnlabeledLine(T line) noexcept
{
    static_assert(std::is_integral_v<T>, "line numbers must be integral");
    if constexpr (std::is_signed_v<T>)
    {
        return line <= 0;
    }
    else
    {
        return line == 0;
    }
}

/// @brief Determine whether a BASIC line label was explicitly provided.
/// @tparam T Integral type representing the stored line number.
/// @param line Candidate line label.
/// @return True when a positive user-specified line label exists.
template <typename T> [[nodiscard]] constexpr bool hasUserLine(T line) noexcept
{
    return !isUnlabeledLine(line);
}

} // namespace il::frontends::basic
