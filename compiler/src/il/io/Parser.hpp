//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Parser class, which reads IL source text and constructs
// Module objects. The parser implements the complete IL grammar defined in
// docs/il-guide.md, enabling IL files to be read from disk, transmitted between
// tools, and inspected during debugging.
//
// The Parser is the inverse of the Serializer: where Serializer converts Module
// → text, Parser converts text → Module. Together they enable round-trip testing,
// textual IL file formats, and human inspection of IL code.
//
// Key Responsibilities:
// - Lexical analysis: Tokenize IL text into keywords, identifiers, literals
// - Syntax parsing: Build Module structure from token stream
// - Type resolution: Parse type specifiers (i64, f64, str, etc.)
// - Value parsing: Constants, temporaries, globals, null pointers
// - Instruction parsing: Opcodes with type-specific operand layouts
// - Error reporting: Precise line/column locations with diagnostic messages
//
// Parser Architecture:
// The parser uses a hand-written recursive descent strategy with dedicated
// sub-parsers for modules, functions, instructions, and operands. This modular
// design keeps parsing logic maintainable and enables precise error recovery.
//
// Internal components (in il/internal/io/):
// - ParserState: Lexer state, token buffer, error accumulation
// - ModuleParser: Top-level module structure (version, target, externs, globals)
// - FunctionParser: Function signatures and basic block structure
// - InstrParser: Instruction opcodes and operand parsing
// - OperandParser: Value parsing (temporaries, constants, addresses)
// - TypeParser: IL type parsing
//
// Error Handling:
// Parse errors are reported via the Expected<T> type. On failure, the parser
// returns diagnostic information including file location, error message, and
// context. The parser attempts to continue after errors to report multiple
// issues in a single pass (best-effort recovery).
//
// Usage Example:
//   std::ifstream file("program.il");
//   Module m;
//   auto result = Parser::parse(file, m);
//   if (!result) {
//     std::cerr << "Parse error: " << result.error() << "\n";
//     return 1;
//   }
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
