//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/FormatUtils.cpp
// Purpose: Provide locale-neutral integer and floating-point formatting
//          routines used by the textual IL serializer.
// Key invariants: Outputs must match the runtime's canonical spellings for
//                 NaN/Inf and retain round-trippable precision for doubles
//                 while avoiding locale-provided thousands separators.
// Ownership/Lifetime: Helpers allocate new std::string instances and return
//                     them by value; no global state is cached.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements locale-stable numeric formatting helpers for textual IL.
/// @details The helpers are kept in a standalone translation unit so that the
///          serializer can format values without pulling in `<locale>` and
///          `<sstream>` from header code.  They encode the runtime's special
///          cases for NaN/Inf and use `max_digits10` precision to guarantee that
///          parsed IEEE 754 doubles survive a print/parse round-trip.

#include "viper/il/io/FormatUtils.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace viper::il::io
{
/// @brief Format a signed 64-bit integer using decimal digits.
/// @details Defers to `std::to_string`, which is locale-neutral for integer
///          formatting and therefore safe for use inside the serializer.  The
///          helper exists so callers can depend solely on this translation unit
///          without including `<string>` or `<sstream>` from headers.
/// @param value Integer value to print in base 10.
/// @return Newly allocated string containing the decimal representation of
///         @p value.
std::string format_integer(std::int64_t value)
{
    return std::to_string(value);
}

/// @brief Format a double in a locale-invariant manner suitable for IL text.
/// @details Mirrors the runtime's textual spellings for special values:
///          `NaN`, `Inf`, and `-Inf`.  Finite values are streamed with the
///          classic "C" locale and `max_digits10` precision so that subsequent
///          parsing reproduces the original bit pattern.  The formatter uses the
///          default floating representation, letting the standard library choose
///          fixed or scientific notation as appropriate.
/// @param value Floating-point value to format.
/// @return Newly allocated string containing the textual representation of
///         @p value.
std::string format_float(double value)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-Inf" : "Inf";

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10)
           << std::defaultfloat << value;
    return stream.str();
}

} // namespace viper::il::io
