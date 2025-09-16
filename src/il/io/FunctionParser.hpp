// File: src/il/io/FunctionParser.hpp
// Purpose: Declares helpers for parsing IL function definitions.
// Key invariants: Requires ParserState to track current function and block context.
// Ownership/Lifetime: Populates the module held by ParserState with parsed functions.
// Links: docs/il-spec.md
#pragma once

#include "il/io/ParserState.hpp"

#include <istream>
#include <ostream>
#include <string>

namespace il::io::detail
{

/// @brief Parse a function header introducing parameters and return type.
bool parseFunctionHeader(const std::string &header, ParserState &st, std::ostream &err);

/// @brief Parse a basic block label and its optional parameter list.
bool parseBlockHeader(const std::string &header, ParserState &st, std::ostream &err);

/// @brief Parse an entire function body following its header line.
bool parseFunction(std::istream &is, std::string &header, ParserState &st, std::ostream &err);

} // namespace il::io::detail
