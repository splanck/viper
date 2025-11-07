//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/IdentifierCase.hpp
// Purpose: Provide helpers for normalising BASIC identifiers to a canonical
//          case so lookups can be performed case-insensitively.
// Key invariants: Only ASCII alphabetic characters are uppercased; digits and
//                 sigils remain untouched to preserve suffix semantics.
// Ownership/Lifetime: Header-only utilities with no retained state.
// Links: docs/codemap.md

#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace il::frontends::basic
{

/// @brief Convert a BASIC identifier to its canonical uppercase form.
/// @details Iterates the supplied text, uppercasing ASCII alphabetic
///          characters while leaving digits, underscores, and type suffix
///          sigils unchanged.  The helper returns a new std::string so callers
///          can retain the original spelling when needed.
/// @param text Identifier spelling to normalise.
/// @return Uppercased identifier suitable for case-insensitive comparisons.
inline std::string canonicalizeIdentifier(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text)
    {
        unsigned char byte = static_cast<unsigned char>(ch);
        result.push_back(static_cast<char>(std::toupper(byte)));
    }
    return result;
}

/// @brief In-place variant of @ref canonicalizeIdentifier.
/// @details Mutates the provided string by uppercasing ASCII alphabetic
///          characters.  Non-alphabetic bytes are left untouched so suffix
///          semantics continue to function as expected.
/// @param text Identifier to transform.
inline void canonicalizeIdentifierInPlace(std::string &text)
{
    for (char &ch : text)
    {
        unsigned char byte = static_cast<unsigned char>(ch);
        ch = static_cast<char>(std::toupper(byte));
    }
}

} // namespace il::frontends::basic

