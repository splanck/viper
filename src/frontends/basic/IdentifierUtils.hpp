// File: src/frontends/basic/IdentifierUtils.hpp
// Purpose: Provide helpers for canonicalizing BASIC identifiers.
// Key invariants: Canonical form uppercases ASCII letters while preserving
//                 non-letter characters.
// Ownership/Lifetime: Header-only utilities; no owning state.
// Links: docs/codemap.md
#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace il::frontends::basic
{

/// @brief Convert a BASIC identifier to its canonical form.
/// @details Uppercases ASCII letters while leaving other characters unchanged
///          so identifiers compare case-insensitively across the frontend.
/// @param text Identifier to canonicalize.
/// @return Uppercased identifier string.
[[nodiscard]] inline std::string canonicalizeIdentifier(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (unsigned char byte : text)
    {
        result.push_back(static_cast<char>(std::toupper(byte)));
    }
    return result;
}

/// @brief Canonicalize a BASIC identifier in-place.
/// @details Applies @ref canonicalizeIdentifier semantics directly to the
///          supplied string to avoid intermediate allocations at call sites.
/// @param text Identifier string mutated to its canonical form.
inline void canonicalizeIdentifierInPlace(std::string &text)
{
    for (char &ch : text)
    {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
}

} // namespace il::frontends::basic

