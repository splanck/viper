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

/// @file
/// @brief Implements symbol mangling helpers for linkable names.
/// @details Provides deterministic conversions between user-facing qualified
///          identifiers and ASCII-safe linker symbols. The mapping is stable
///          across platforms and is intentionally lossy so that any unknown
///          characters collapse into a safe separator.

#include "common/Mangle.hpp"

#include <cctype>

namespace viper::common
{

/// @brief Convert a dotted qualified name into a safe linker symbol.
/// @details The transformation lowercases all ASCII letters, prefixes the
///          result with '@', and replaces dots with underscores so nested names
///          are preserved in a flat namespace. Any non-alphanumeric characters
///          (other than underscore) are mapped to '_' to keep output strictly
///          ASCII and deterministic across hosts.
/// @param qualified Qualified name such as "A.B.Func" or "Klass.__ctor".
/// @return Mangled ASCII symbol safe to pass to the native toolchain.
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

/// @brief Best-effort conversion from a mangled linker symbol to dotted form.
/// @details Removes a leading '@' when present and replaces underscores with
///          dots. The conversion does not restore original casing or special
///          characters because the forward mapping is intentionally lossy; the
///          goal is a human-readable, stable identifier for diagnostics.
/// @param symbol Mangled symbol such as "@a_b_func".
/// @return A dotted identifier such as "a.b.func".
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
