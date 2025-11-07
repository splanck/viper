//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/IdentifierUtils.hpp
// Purpose: Provide helper routines for normalising BASIC identifiers to their
//          canonical uppercase representation and performing case-insensitive
//          comparisons.
// Key invariants: Canonicalisation is limited to ASCII letters as BASIC source
//                 is restricted to that subset. Lengths are preserved by the
//                 transformation so suffix-based type inference remains valid.
// Ownership/Lifetime: Header-only utilities with no state; safe to include
//                     freely across translation units.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace il::frontends::basic
{

/// @brief Convert a BASIC identifier to its canonical uppercase form.
/// @details The helper copies the supplied text and uppercases ASCII letters,
///          leaving digits and suffix sigils untouched.  It is intended for
///          normalising hash table keys so lookups become case-insensitive.
/// @param text Identifier spelling to canonicalise.
/// @return Uppercased copy of @p text.
[[nodiscard]] inline std::string canonicalizeIdentifier(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (unsigned char ch : text)
        result.push_back(static_cast<char>(std::toupper(ch)));
    return result;
}

/// @brief Canonicalise a BASIC identifier in place.
/// @details Mutates the supplied string by uppercasing ASCII letters.  Useful
///          when the caller already owns a mutable buffer and wishes to avoid an
///          extra allocation.
/// @param text Identifier string to canonicalise.
inline void canonicalizeIdentifierInPlace(std::string &text)
{
    for (char &ch : text)
    {
        unsigned char byte = static_cast<unsigned char>(ch);
        ch = static_cast<char>(std::toupper(byte));
    }
}

/// @brief Compare two identifier spellings ignoring case differences.
/// @details Performs a length check followed by a character-wise comparison of
///          the uppercase forms without creating temporary strings.  Suitable
///          for hotspots where avoiding allocations matters.
/// @param lhs First identifier.
/// @param rhs Second identifier.
/// @return True when @p lhs and @p rhs are equal ignoring ASCII case.
[[nodiscard]] inline bool identifiersEqual(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        unsigned char left = static_cast<unsigned char>(lhs[i]);
        unsigned char right = static_cast<unsigned char>(rhs[i]);
        if (std::toupper(left) != std::toupper(right))
            return false;
    }
    return true;
}

} // namespace il::frontends::basic

