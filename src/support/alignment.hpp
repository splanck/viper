//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/alignment.hpp
// Purpose: Provides alignment utilities for memory and offset calculations.
//
// The alignUp function rounds a value up to the next multiple of a given
// alignment. This is commonly needed for stack frame layout, memory allocation,
// and data structure padding.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace il::support
{

/// @brief Round a value up to the next multiple of alignment.
/// @details Computes the smallest value >= n that is a multiple of alignment.
///          Uses the standard bit manipulation formula: (n + align - 1) & ~(align - 1)
///          Alignment must be a power of two for correct results.
/// @tparam T Integral type of the value being aligned.
/// @param n Value to align.
/// @param alignment Alignment boundary (must be power of two).
/// @return Smallest value >= n that is a multiple of alignment.
template <typename T> [[nodiscard]] constexpr T alignUp(T n, T alignment) noexcept
{
    static_assert(std::is_integral_v<T>, "alignUp requires an integral type");
    return (n + alignment - 1) & ~(alignment - 1);
}

/// @brief Check if a value is aligned to a given boundary.
/// @tparam T Integral type of the value being checked.
/// @param n Value to check.
/// @param alignment Alignment boundary (must be power of two).
/// @return true if n is a multiple of alignment.
template <typename T> [[nodiscard]] constexpr bool isAligned(T n, T alignment) noexcept
{
    static_assert(std::is_integral_v<T>, "isAligned requires an integral type");
    return (n & (alignment - 1)) == 0;
}

} // namespace il::support
