//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/TypeSuffix.hpp
// Purpose: Declares helpers for inferring BASIC semantic types from identifier suffixes. 
// Key invariants: BASIC suffix characters map to a single AST scalar type.
// Ownership/Lifetime: Pure utility with no retained state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"

#include <optional>
#include <string_view>

namespace il::frontends::basic
{

/// @brief Determine the BASIC AST type implied by an identifier suffix.
/// @param name BASIC identifier, potentially containing a type suffix character.
/// @return Semantic type derived from the suffix, defaulting to integer when none matches.
Type inferAstTypeFromName(std::string_view name);

/// @brief Inspect @p name and return the AST type encoded by a recognised suffix.
/// @param name Identifier spelling to analyse.
/// @return Optional semantic type when the suffix matches one of BASIC's sigils.
std::optional<Type> inferAstTypeFromSuffix(std::string_view name);

} // namespace il::frontends::basic
