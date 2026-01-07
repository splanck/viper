//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/il/io/FormatUtils.hpp
// Purpose: Declare formatting helpers used by IL-level utilities and frontends.
// Key invariants: Formatting remains locale-independent and matches runtime
//                 conventions for special values.
// Ownership/Lifetime: Returned strings own their storage and are independent
//                     of the helpers.
// License: GPL-3.0-only (see LICENSE).
// Links: docs/il-guide.md#reference
#pragma once

#include <cstdint>
#include <string>

namespace viper::il::io
{

/// @brief Format a signed 64-bit integer using BASIC's canonical decimal form.
/// @param value Value to convert to text.
/// @return Decimal string representation without locale influence.
[[nodiscard]] std::string format_integer(std::int64_t value);

/// @brief Format a double-precision floating-point value for BASIC literals.
/// @param value Floating-point value to convert to text.
/// @return Locale-independent representation that preserves round-trip fidelity.
[[nodiscard]] std::string format_float(double value);

} // namespace viper::il::io
