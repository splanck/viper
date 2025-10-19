// File: src/frontends/basic/TypeSuffix.hpp
// Purpose: Declares helpers for inferring BASIC semantic types from identifier suffixes.
// Key invariants: BASIC suffix characters map to a single AST scalar type.
// Ownership/Lifetime: Pure utility with no retained state.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include <string_view>

namespace il::frontends::basic
{

/// @brief Determine the BASIC AST type implied by an identifier suffix.
/// @param name BASIC identifier, potentially containing a type suffix character.
/// @return Semantic type derived from the suffix, defaulting to integer when none matches.
Type inferAstTypeFromName(std::string_view name);

} // namespace il::frontends::basic
