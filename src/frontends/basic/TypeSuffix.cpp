//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/TypeSuffix.cpp
// Purpose: Provide the BASIC identifier suffix parser that converts sigils into
//          semantic types used by the lowering pipeline.
// Key invariants: Missing suffixes default to the integer type to match legacy
//                 BASIC semantics.
// Links: docs/basic-language.md#types
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the mapping between BASIC type suffixes and semantic types.
/// @details Consolidating the logic here avoids exposing parsing helpers in the
///          public header while clearly documenting the defaulting rules applied
///          when a suffix is absent.

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
std::optional<Type> inferAstTypeFromSuffix(std::string_view name)
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
    return std::nullopt;
}

Type inferAstTypeFromName(std::string_view name)
{
    if (auto suffixType = inferAstTypeFromSuffix(name))
        return *suffixType;
    return Type::I64;
}

} // namespace il::frontends::basic
