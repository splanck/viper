// File: src/frontends/basic/TypeSuffix.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements helpers for inferring BASIC semantic types from identifier suffixes.
// Key invariants: BASIC suffix characters map deterministically to AST scalar types.
// Ownership/Lifetime: Stateless utility functions.
// Links: docs/codemap.md

#include "frontends/basic/TypeSuffix.hpp"

namespace il::frontends::basic
{

Type inferAstTypeFromName(std::string_view name)
{
    if (!name.empty())
    {
        switch (name.back())
        {
            case '$':
                return Type::Str;
            case '#':
                return Type::F64;
            default:
                break;
        }
    }
    return Type::I64;
}

} // namespace il::frontends::basic
