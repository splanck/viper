//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Token.cpp
// Purpose: Implement token-to-string conversion helpers used by diagnostics and
//          debugging tools in the BASIC front end.
// Key invariants: The generated table always matches TokenKind::Count.
// Links: docs/basic-language.md#lexical-structure
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Houses the lookup table that maps token kinds to display strings.
/// @details The mapping is generated from TokenKinds.def so that token spelling
///          updates propagate automatically.  Centralising the implementation
///          avoids duplicating the table across translation units.

#include "frontends/basic/Token.hpp"

#include <cstddef>

namespace il::frontends::basic
{
namespace
{
constexpr const char *kTokenNames[] = {
#define TOKEN(K, S) S,
#include "frontends/basic/TokenKinds.def"
#undef TOKEN
};

constexpr std::size_t kTokenNameCount = sizeof(kTokenNames) / sizeof(kTokenNames[0]);

static_assert(kTokenNameCount == static_cast<std::size_t>(TokenKind::Count),
              "TokenKinds.def and TokenKind are out of sync");
} // namespace

/// @brief Maps a token kind to its canonical string representation.
/// @details Each enumerator in TokenKind is handled via a shared table generated
///          from TokenKinds.def.  Unrecognized values fall back to a "?" marker.
///          A static assertion keeps the table aligned with the enum so missing
///          entries surface during compilation.
/// @param k Token kind to convert.
/// @return Null-terminated string naming @p k, or "?" if no mapping exists.
const char *tokenKindToString(TokenKind k)
{
    const auto index = static_cast<std::size_t>(k);
    if (index < kTokenNameCount)
    {
        return kTokenNames[index];
    }
    return "?";
}

} // namespace il::frontends::basic
