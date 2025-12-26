//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides locale-stable formatting helpers for serialising IL numeric values.
// The routines convert integer and floating-point primitives into canonical
// textual forms used by the serializer, verifier dumps, and diagnostics.  The
// helpers deliberately avoid locale-sensitive facets so output remains
// reproducible regardless of a developer's environment, and they mirror the
// runtime's NaN/Inf spelling so round-trips through textual IL stay lossless.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements canonical numeric formatting helpers for IL serialization.
/// @details Defines @ref format_integer and @ref format_float, two lightweight
///          wrappers that encode numbers into locale-independent strings.  Both
///          functions return owning std::string instances so callers can persist
///          the text without tracking additional lifetimes.

#include "viper/il/io/FormatUtils.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace viper::il::io
{

/// @brief Convert a signed integer into its canonical string representation.
/// @details Delegates to @c std::to_string because the helper already emits
///          locale-independent text for integral values.  Keeping the wrapper in
///          one place ensures callers do not need to reason about locale facets
///          or signed zero handling; the standard library routine covers both
///          concerns.
/// @param value Integer value that should be rendered.
/// @return Decimal string suitable for IL text emission.
std::string format_integer(std::int64_t value)
{
    return std::to_string(value);
}

/// @brief Render a floating-point value using the IL's canonical spelling.
/// @details Applies the following normalisation steps:
///          - Maps quiet NaNs to the literal "NaN".
///          - Maps positive and negative infinities to "Inf"/"-Inf".
///          - Emits all finite numbers with `max_digits10` precision and the
///            C locale to guarantee round-trip fidelity when reparsed.
///          The helper intentionally avoids scientific formatting overrides so
///          downstream consumers receive the default `std::defaultfloat`
///          presentation consistent with runtime printing.
/// @param value Double-precision value to format.
/// @return Locale-independent string representing @p value.
std::string format_float(double value)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-Inf" : "Inf";

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << std::defaultfloat
           << value;
    return stream.str();
}

} // namespace viper::il::io
