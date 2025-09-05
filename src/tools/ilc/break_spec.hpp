// File: src/tools/ilc/break_spec.hpp
// Purpose: Helpers for parsing --break flag specifications.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// Links: docs/class-catalog.md

#pragma once

#include <cctype>
#include <string>

namespace ilc
{

/// @brief Determine whether a --break argument refers to a source line.
///
/// A token is treated as a source specification when it matches "<file>:<line>"
/// and the file portion contains a path separator or a dot.
inline bool isSrcBreakSpec(const std::string &spec)
{
    /// Position of the colon separating file and line.
    auto pos = spec.rfind(':');
    if (pos == std::string::npos || pos + 1 >= spec.size())
        return false;
    /// Ensure all characters after the colon are digits.
    for (size_t i = pos + 1; i < spec.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(spec[i])))
            return false;
    }
    /// Portion before the colon; treated as a file path candidate.
    std::string left = spec.substr(0, pos);
    return left.find('/') != std::string::npos || left.find('\\') != std::string::npos ||
           left.find('.') != std::string::npos;
}

} // namespace ilc
