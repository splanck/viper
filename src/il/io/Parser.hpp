//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/io/Parser.hpp
// Purpose: Declares the Parser class -- reads IL source text and constructs
//          Module objects using hand-written recursive descent. Implements the
//          complete IL grammar (version, target, externs, globals, functions,
//          instructions, operands). Inverse of Serializer.
// Key invariants:
//   - parse() is stateless; safe to call from multiple threads.
//   - On success, the output module conforms to the IL spec.
//   - On failure, returns diagnostic with precise line/column location.
// Ownership/Lifetime: Parser is a stateless facade with a static parse()
//          method. The caller owns the istream and the output Module.
// Links: docs/il-guide.md, il/io/Serializer.hpp,
//        il/internal/io/ParserState.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"
#include "il/internal/io/FunctionParser.hpp"
#include "il/internal/io/InstrParser.hpp"
#include "il/internal/io/ModuleParser.hpp"
#include "il/internal/io/ParserState.hpp"
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
    [[nodiscard]] static il::support::Expected<void> parse(std::istream &is, il::core::Module &m);
};

} // namespace il::io
