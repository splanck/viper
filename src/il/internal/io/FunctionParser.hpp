//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/internal/io/FunctionParser.hpp
// Purpose: Declares helpers for parsing IL function definitions.
// Key invariants: Requires ParserState to track current function and block context.
// Ownership/Lifetime: Populates the module held by ParserState with parsed functions.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/internal/io/ParserState.hpp"
#include "support/diag_expected.hpp"

#include <istream>
#include <string>

namespace il::io::detail
{

/// @brief Parse a function header introducing parameters and return type.
[[nodiscard]] il::support::Expected<void> parseFunctionHeader(const std::string &header,
                                                              ParserState &st);

/// @brief Parse a basic block label and its optional parameter list.
[[nodiscard]] il::support::Expected<void> parseBlockHeader(const std::string &header,
                                                           ParserState &st);

/// @brief Parse an entire function body following its header line.
[[nodiscard]] il::support::Expected<void> parseFunction(std::istream &is,
                                                        std::string &header,
                                                        ParserState &st);

} // namespace il::io::detail
