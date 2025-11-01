// File: src/il/io/FormatUtils.cpp
// Purpose: Define IL-level numeric formatting helpers for locale-independent text.
// Key invariants: Floating-point formatting matches runtime special-case strings
//                 and uses precision sufficient for round-trip fidelity.
// Ownership/Lifetime: Helpers return owning std::string instances.
// License: MIT (see LICENSE).
// Links: docs/il-guide.md#reference

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
    stream << std::setprecision(std::numeric_limits<double>::max_digits10)
           << std::defaultfloat << value;
    return stream.str();
}

} // namespace viper::il::io

