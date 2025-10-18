//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/NameMangler_OOP.hpp
// Purpose: Declare mangling helpers for BASIC class-oriented constructs.
// Key invariants: Mangled names remain stable and purely derived from inputs.
// Ownership/Lifetime: Returns freshly-allocated std::string instances owned by callers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Declares BASIC frontend helpers for mangling class-related symbols.
/// @details These helpers provide a consistent naming convention for class
///          constructors, destructors, and methods so that later lowering
///          stages can rely on stable symbol identifiers irrespective of
///          declaration order or compilation session.

#pragma once

#include <string>
#include <string_view>

namespace il::frontends::basic
{


/// @brief Produce the mangled name for a class constructor symbol.
std::string mangleClassCtor(std::string_view klass);

/// @brief Produce the mangled name for a class destructor symbol.
std::string mangleClassDtor(std::string_view klass);

/// @brief Produce the mangled name for a method symbol scoped to @p klass.
std::string mangleMethod(std::string_view klass, std::string_view method);


} // namespace il::frontends::basic

