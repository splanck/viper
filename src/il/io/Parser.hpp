// File: src/il/io/Parser.hpp
// Purpose: Declares parser for IL textual representation.
// Key invariants: None.
// Ownership/Lifetime: Parser views input string without owning.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/fwd.hpp"
#include "il/io/FunctionParser.hpp"
#include "il/io/InstrParser.hpp"
#include "il/io/ModuleParser.hpp"
#include "il/io/ParserState.hpp"
#include "support/diag_expected.hpp"

#include <istream>

namespace il::io
{

/// @brief Hand-rolled parser for textual IL subset.
class Parser
{
  public:
    /// @brief Parse IL from stream into module @p m.
    /// @param is Input stream containing IL text.
    /// @param m Module to populate with parsed contents.
    /// @return Expected success or diagnostic on failure.
    static il::support::Expected<void> parse(std::istream &is, il::core::Module &m);
};

} // namespace il::io
