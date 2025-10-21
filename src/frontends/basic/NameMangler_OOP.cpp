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


#include <string>
#include <string_view>

namespace il::frontends::basic
{

namespace
{
/// @brief Concatenate a class name with a suffix that marks a special role.
///
/// @details Reserves the combined length up-front to avoid reallocations before
///          appending the raw character sequences from @p base and @p suffix.
///          The helper keeps mangling logic local to this file so the public
///          helpers can focus on semantic naming rules.
///
/// @param base   Unmangled class identifier.
/// @param suffix Role suffix such as ".__ctor" or ".__dtor".
/// @return Stable mangled identifier string.
std::string joinWithSuffix(std::string_view base, std::string_view suffix)
{
    std::string result;
    result.reserve(base.size() + suffix.size());
    result.append(base.begin(), base.end());
    result.append(suffix.begin(), suffix.end());
    return result;
}

/// @brief Concatenate two identifier components using a dot separator.
///
/// @details Builds the output string in a single allocation by reserving the
///          combined length and inserting the separator explicitly.  Used by the
///          method mangler so the scheme matches BASIC's surface syntax while
///          remaining deterministic.
///
/// @param lhs Class portion of the mangled identifier.
/// @param rhs Member portion (method name) of the identifier.
/// @return Joined identifier string in @c "Class.Member" form.
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

/// @brief Build the mangled constructor name for a BASIC class.
///
/// @details Constructors receive a stable ".__ctor" suffix to distinguish them
///          from user-defined methods while keeping the human-readable prefix
///          intact.
///
/// @param klass Source class name.
/// @return Mangled constructor symbol name.
std::string mangleClassCtor(std::string_view klass)
{
    return joinWithSuffix(klass, ".__ctor");
}

/// @brief Build the mangled destructor name for a BASIC class.
///
/// @details Mirrors @ref mangleClassCtor but uses the ".__dtor" suffix so the
///          lowering logic can reliably locate destructor helpers.
///
/// @param klass Source class name.
/// @return Mangled destructor symbol name.
std::string mangleClassDtor(std::string_view klass)
{
    return joinWithSuffix(klass, ".__dtor");
}

/// @brief Construct the mangled identifier for an instance method.
///
/// @details Inserts a dot between the class and method names, matching BASIC's
///          surface syntax while producing a single symbol suitable for IL and
///          runtime lookup.
///
/// @param klass  Source class name.
/// @param method Method identifier.
/// @return Mangled method symbol name.
std::string mangleMethod(std::string_view klass, std::string_view method)
{
    return joinWithDot(klass, method);
}

} // namespace il::frontends::basic


