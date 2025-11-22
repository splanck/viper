//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/LabelUtil.hpp
// Purpose: Small helpers for generating assembler-safe labels.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

namespace viper::codegen::common
{

// Sanitize an arbitrary name into an assembler-safe label.
// - Permits [A-Za-z0-9_.$]
// - Drops '-'
// - Replaces other characters with '_'
// - Ensures label does not start with a digit by prefixing 'L' if needed
// - Optionally appends a suffix verbatim (useful for uniquifying)
inline std::string sanitizeLabel(std::string_view in, std::string_view suffix = {})
{
    std::string out;
    out.reserve(in.size() + suffix.size() + 2);
    for (unsigned char ch : in)
    {
        const bool isAlpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool isDigit = (ch >= '0' && ch <= '9');
        if (isAlpha || isDigit || ch == '_' || ch == '.' || ch == '$')
        {
            out.push_back(static_cast<char>(ch));
        }
        else if (ch == '-')
        {
            // drop
        }
        else
        {
            out.push_back('_');
        }
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9'))
        out.insert(out.begin(), 'L');
    if (!suffix.empty())
    {
        out.append(suffix);
    }
    return out;
}

} // namespace viper::codegen::common

