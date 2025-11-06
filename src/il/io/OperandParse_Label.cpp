//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

/// @brief Construct a parse result representing a fatal syntax error.
/// @details Wraps the provided @p message in an @ref il::support::Expected error
///          that carries the current parser location.  Centralising the error
///          creation keeps diagnostics consistent with other operand parsers.
/// @param ctx Parser context containing source location metadata.
/// @param message Human-readable diagnostic to report.
/// @return Parse result populated with the error state.
ParseResult syntaxError(Context &ctx, std::string message)
{
    ParseResult result;
    result.status = ::il::support::Expected<void>{::il::support::makeError(
        ctx.state.curLoc, ::il::io::formatLineDiag(ctx.state.lineNo, std::move(message)))};
    return result;
}

} // namespace

/// @brief Parse a branch label operand from the remaining cursor text.
/// @details Normalises whitespace, strips optional ``label`` keywords and caret
///          prefixes, and traps malformed inputs via @ref syntaxError.  On
///          success the helper consumes the rest of the cursor to avoid
///          downstream parsers reprocessing the label text.
/// @param cur Cursor positioned at the operand text.
/// @param ctx Parser context receiving diagnostics and results.
/// @return Successful parse result containing the label text, or an error when
///         the operand is malformed.
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

