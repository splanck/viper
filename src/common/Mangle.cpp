//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/common/Mangle.cpp
// Purpose: Implement unified mangling for linkable symbols derived from
//          dot-qualified names used across frontends and OOP emission.
// Key invariants:
//   - Output is lowercase ASCII, starts with '@', and uses '_' as separator.
//   - Input may include dots and underscores; unsupported chars map to '_'.
// Ownership/Lifetime: Stateless helpers returning std::string by value.
// Links: src/common/Mangle.hpp
//
//===----------------------------------------------------------------------===//

#include "common/Mangle.hpp"

#include <cctype>

namespace viper::common
{

std::string MangleLink(std::string_view qualified)
{
    std::string out;
    out.reserve(qualified.size() + 1);
    out.push_back('@');
    for (unsigned char ch : qualified)
    {
        if (ch == '.')
        {
            out.push_back('_');
        }
        else if (std::isalnum(ch) || ch == '_')
        {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
        else
        {
            // Map other characters (e.g., '$') to underscore deterministically
            out.push_back('_');
        }
    }
    return out;
}

std::string DemangleLink(std::string_view symbol)
{
    std::string_view body = symbol;
    if (!body.empty() && body.front() == '@')
        body.remove_prefix(1);
    std::string out;
    out.reserve(body.size());
    for (unsigned char ch : body)
    {
        out.push_back(ch == '_' ? '.' : static_cast<char>(ch));
    }
    return out;
}

} // namespace viper::common
