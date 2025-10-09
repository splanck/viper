//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the helper that interprets traditional BASIC type suffix characters
// (`$`, `%`, `&`, `!`, `#`) and maps them onto the compiler's scalar type
// enumeration. Keeping the logic out of the header keeps the inline surface
// small and lets the implementation document the fallbacks used when a name is
// not suffixed.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/TypeSuffix.hpp"

namespace il::frontends::basic
{

/// @brief Deduce the BASIC scalar type represented by an identifier suffix.
///
/// BASIC allows variable names to end in a sigil that encodes the variable's
/// type (for example `A$` for strings and `B%` for integers). The lowering
/// pipeline models those choices through the `Type` enumeration. This helper
/// inspects the final character of @p name, returning the corresponding
/// semantic type. Names without a suffix default to `Type::I64`, mirroring the
/// semantics of classic BASIC dialects.
///
/// @param name Identifier to inspect; the view is not stored.
/// @return The inferred type based on the final character, or `Type::I64` when
///         no suffix is present.
Type inferAstTypeFromName(std::string_view name)
{
    if (!name.empty())
    {
        switch (name.back())
        {
            case '$':
                return Type::Str;
            case '#':
            case '!':
                return Type::F64;
            case '%':
            case '&':
                return Type::I64;
            default:
                break;
        }
    }
    return Type::I64;
}

} // namespace il::frontends::basic

