// File: src/tools/ilc/break_spec.cpp
// Purpose: Implementation helpers for parsing --break flag specifications.
// Key invariants: Matches syntactic checks defined in header documentation.
// Ownership/Lifetime: N/A.
// Links: docs/codemap.md

#include "break_spec.hpp"

#include <cctype>

namespace ilc
{

bool isSrcBreakSpec(const std::string &spec)
{
    /// Position of the colon separating file and line.
    auto pos = spec.rfind(':');
    if (pos == std::string::npos || pos + 1 >= spec.size())
        return false;
    /// Ensure all characters after the colon are digits.
    for (std::string::size_type i = pos + 1; i < spec.size(); ++i)
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
