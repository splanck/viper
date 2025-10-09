//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the string conversion helper for BASIC tokens.  Keeping the
// implementation here avoids re-parsing the token list in multiple translation
// units while documenting the invariants around TokenKinds.def.
//
//===----------------------------------------------------------------------===//

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

/**
 * @brief Maps a token kind to its canonical string representation.
 *
 * Each enumerator in TokenKind is handled via a shared table generated from
 * TokenKinds.def. Unrecognized values fall back to a "?" marker. The table is
 * kept in sync with TokenKind via a static assertion to preserve completeness.
 *
 * @param k Token kind to convert.
 * @return Null-terminated string naming @p k, or "?" if no mapping exists.
 */
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
