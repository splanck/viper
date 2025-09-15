// File: src/il/io/detail/InstructionHandlers.hpp
// Purpose: Declares opcode-specific parsing helpers for the IL parser.
// Key invariants: Handlers assume parser state invariants from ParserState.hpp.
// Ownership/Lifetime: Operate on caller-provided state and instruction objects.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/io/detail/ParserState.hpp"
#include <functional>
#include <ostream>
#include <string>
#include <unordered_map>

namespace il::io::detail
{

/// @brief Signature for individual opcode parsing callbacks.
using InstrHandler =
    std::function<bool(const std::string &, core::Instr &, ParserState &, std::ostream &)>;

/// @brief Parse a textual type token into an IL Type instance.
/// @param token Lowercase token such as "i64", "ptr", or "void".
/// @param ok Optional flag updated to reflect success.
/// @return Parsed type or default Type on failure.
core::Type parseType(const std::string &token, bool *ok = nullptr);

/// @brief Parse a textual value token.
/// @param token Token representing constants, temporaries, or globals.
/// @param state Parser state containing symbol tables and diagnostics info.
/// @param err Stream receiving diagnostic messages.
/// @return Parsed Value object; errors are reported through @p err.
core::Value parseValue(const std::string &token, ParserState &state, std::ostream &err);

/// @brief Access the opcode dispatch table used by the parser.
/// @return Mapping from opcode mnemonics to handler callbacks.
const std::unordered_map<std::string, InstrHandler> &instructionHandlers();

} // namespace il::io::detail
