// File: src/tools/ilc/break_parse.hpp
// Purpose: Define helper for interpreting --break tokens.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// Links: docs/class-catalog.md
#pragma once
#include <cctype>
#include <string_view>

namespace ilc
{
/// @brief Determine if @p spec denotes a source line breakpoint.
inline bool isBreakSrcSpec(std::string_view spec)
{
    auto pos = spec.rfind(':');
    if (pos == std::string_view::npos || pos + 1 >= spec.size())
        return false;
    for (size_t i = pos + 1; i < spec.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(spec[i])))
            return false;
    }
    std::string_view left = spec.substr(0, pos);
    return left.find('/') != std::string_view::npos || left.find('\\') != std::string_view::npos ||
           left.find('.') != std::string_view::npos;
}
} // namespace ilc
