//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/OperandParse_Label.cpp
// Purpose: Provide the per-kind parser for IL branch labels.
// Key invariants: Matches OperandParser's label normalisation and diagnostics.
// Ownership/Lifetime: Operates on parser-managed state without owning data.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the helper that parses branch label operands.
/// @details The helper preserves the legacy trimming rules for optional
///          "label" keywords and caret prefixes while emitting the same
///          diagnostics for malformed input.

#include "viper/il/io/OperandParse.hpp"

#include "il/internal/io/ParserState.hpp"
#include "il/internal/io/ParserUtil.hpp"

#include <string>
#include <utility>

namespace viper::il::io
{
namespace
{
using viper::parse::Cursor;

/// @brief Build a ParseResult describing a label syntax error.
/// @details Populates the result with a diagnostic carrying the provided
///          message and source location taken from the parser context.  Keeping
///          the helper local avoids duplicating the diagnostic wiring across the
///          various early-exit sites in @ref parseLabelOperand.
/// @param ctx Parsing context containing source location and diagnostics sink.
/// @param message Human-readable description of the syntax problem.
/// @return ParseResult initialised with an error status and message.
ParseResult syntaxError(Context &ctx, std::string message)
{
    ParseResult result;
    result.status = ::il::support::Expected<void>{
        ::il::io::makeLineErrorDiag(ctx.state.curLoc, ctx.state.lineNo, std::move(message))};
    return result;
}

} // namespace

/// @brief Parse an IL operand that names a branch label.
/// @details Consumes the remaining characters in the cursor, trims whitespace,
///          strips optional `label` keywords and caret prefixes, and validates
///          that a non-empty identifier remains.  Successful parses return the
///          canonicalised label text while failures route through
///          @ref syntaxError to surface consistent diagnostics.
/// @param cur Cursor positioned at the operand start; advanced to the end.
/// @param ctx Parsing context providing diagnostic helpers and state.
/// @return ParseResult containing the parsed label or an error diagnostic.
ParseResult parseLabelOperand(Cursor &cur, Context &ctx)
{
    std::string text(cur.remaining());
    text = ::il::io::trim(text);
    if (text.rfind("label ", 0) == 0)
        text = ::il::io::trim(text.substr(6));

    if (!text.empty() && text.front() == '^')
        text.erase(text.begin());

    text = ::il::io::trim(text);
    if (text.empty())
        return syntaxError(ctx, "malformed branch target: missing label");

    cur.consumeRest();
    ParseResult result;
    result.label = std::move(text);
    return result;
}

} // namespace viper::il::io
