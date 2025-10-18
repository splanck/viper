//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/NameMangler_OOP.cpp
// Purpose: Implement BASIC class symbol mangling helpers for OOP features.
// Key invariants: Mangled outputs must remain deterministic for identical inputs.
// Ownership/Lifetime: Functions return std::string values owned by the caller.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Defines helpers responsible for constructing stable identifiers for
///        BASIC class members.
/// @details The mangling scheme mirrors BASIC's surface identifiers while
///          encoding member roles (constructor, destructor, or method) via
///          suffixes.  This keeps downstream lowering agnostic to the source
///          syntax while allowing straightforward symbol lookups.

#include "frontends/basic/NameMangler_OOP.hpp"

#if VIPER_ENABLE_OOP

#include <string>
#include <string_view>

namespace il::frontends::basic
{

namespace
{
std::string joinWithSuffix(std::string_view base, std::string_view suffix)
{
    std::string result;
    result.reserve(base.size() + suffix.size());
    result.append(base.begin(), base.end());
    result.append(suffix.begin(), suffix.end());
    return result;
}

std::string joinWithDot(std::string_view lhs, std::string_view rhs)
{
    std::string result;
    result.reserve(lhs.size() + rhs.size() + 1);
    result.append(lhs.begin(), lhs.end());
    result.push_back('.');
    result.append(rhs.begin(), rhs.end());
    return result;
}
} // namespace

std::string mangleClassCtor(std::string_view klass)
{
    return joinWithSuffix(klass, ".__ctor");
}

std::string mangleClassDtor(std::string_view klass)
{
    return joinWithSuffix(klass, ".__dtor");
}

std::string mangleMethod(std::string_view klass, std::string_view method)
{
    return joinWithDot(klass, method);
}

} // namespace il::frontends::basic

#endif // VIPER_ENABLE_OOP

