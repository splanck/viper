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

#include <string>
#include <utility>

namespace viper::il::io
{
namespace
{
using viper::parse::Cursor;

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
