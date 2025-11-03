// File: src/il/internal/io/ModuleParser.hpp
// Purpose: Declares helpers for parsing module-level IL directives.
// Key invariants: Operates with ParserState positioned at module scope.
// Ownership/Lifetime: Updates module metadata and dispatches to function parsing.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/internal/io/ParserState.hpp"

#include <istream>
#include <ostream>
#include <string>

namespace il::io::detail
{

/// @brief Parse a single top-level directive such as extern, global, or func.
bool parseModuleHeader(std::istream &is, std::string &line, ParserState &st, std::ostream &err);

} // namespace il::io::detail
