//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/FormatUtils.cpp
// Purpose: Provide locale-independent numeric formatting helpers for IL tools.
// Key invariants: Avoid runtime dependencies while matching BASIC constant
//                 folding semantics for special floating-point values.
// Ownership/Lifetime: Return owning std::string values without cached state.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements IL-level numeric formatters for frontend utilities.
/// @details Supplies simple decimal formatting helpers that keep the IL layer
///          self-contained.  Floating-point formatting uses round-trip precision
///          to avoid lossy conversions, handles infinities/NaN explicitly, and
///          leverages the classic C locale to guarantee '.' as the decimal
///          separator.

#include "viper/il/io/FormatUtils.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace viper::il::io
{

std::string format_integer(std::int64_t value)
{
    return std::to_string(value);
}

std::string format_float(double value)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-Inf" : "Inf";

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream.setf(std::ios::fmtflags(0), std::ios::floatfield);
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

} // namespace viper::il::io

