// File: src/il/io/Parser.hpp
// Purpose: Declares parser for IL textual representation.
// Key invariants: None.
// Ownership/Lifetime: Parser views input string without owning.
// Links: docs/il-spec.md
#pragma once

#include "il/core/fwd.hpp"
#include "il/io/FunctionParser.hpp"
#include "il/io/InstrParser.hpp"
#include "il/io/ModuleParser.hpp"
#include "il/io/ParserState.hpp"

#include <istream>
#include <ostream>

namespace il::io
{

/// @brief Hand-rolled parser for textual IL subset.
class Parser
{
  public:
    /// @brief Parse IL from stream into module @p m.
    /// @param is Input stream containing IL text.
    /// @param m Module to populate with parsed contents.
    /// @param err Diagnostic output stream.
    /// @return True on success, false if parse errors occurred.
    static bool parse(std::istream &is, il::core::Module &m, std::ostream &err);
};

} // namespace il::io
