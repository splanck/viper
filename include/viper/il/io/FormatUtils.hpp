//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/il/io/FormatUtils.hpp
// Purpose: Declare lightweight formatting helpers for IL-level utilities.
// Key invariants: Provide stable, locale-independent conversions for numeric
//                 constants without depending on the runtime layer.
// Ownership/Lifetime: Return owning std::string instances; no retained state.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Declares IL-layer numeric formatting helpers used by frontends.
/// @details The utilities provide minimal string conversions for integers and
///          floating-point values that mirror BASIC constant folding semantics
///          without introducing a dependency on the runtime formatting
///          library.  Output is locale independent and suitable for embedding
///          into IL text or frontend diagnostics.

#pragma once

#include <cstdint>
#include <string>

namespace viper::il::io
{

/// @brief Format an integer value using decimal notation.
/// @param value Signed integer to format.
/// @return Decimal string representation of @p value.
[[nodiscard]] std::string format_integer(std::int64_t value);

/// @brief Format a double value using round-trip precision.
/// @param value Floating-point number to format.
/// @return Locale-independent representation that preserves round-trip
///         semantics and matches BASIC STR folding behaviour for special
///         values.
[[nodiscard]] std::string format_float(double value);

} // namespace viper::il::io

