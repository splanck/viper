// File: src/il/internal/io/TypeParser.hpp
// Purpose: Declares helpers for parsing textual IL type specifiers.
// Key invariants: Type identifiers adhere to docs/il-guide.md#reference definitions.
// Ownership/Lifetime: Returned Type objects belong to callers.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Type.hpp"

#include <string>

namespace il::io
{

/// @brief Parse a textual type token into its IL representation.
/// @param token Lowercase token naming a primitive IL type.
/// @param ok Optional flag receiving true on success and false on failure.
/// @return Parsed il::core::Type value or default constructed on failure.
il::core::Type parseType(const std::string &token, bool *ok = nullptr);

} // namespace il::io
