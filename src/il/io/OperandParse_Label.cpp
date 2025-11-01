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

#include "il/io/ParserState.hpp"
#include "il/io/ParserUtil.hpp"

#include <string>
#include <utility>

namespace viper::il::io
{
namespace
{
using viper::parse::Cursor;

ParseResult syntaxError(Context &ctx, std::string message)
{
    ParseResult result;
    result.status = ::il::support::Expected<void>{::il::support::makeError(
        ctx.state.curLoc, ::il::io::formatLineDiag(ctx.state.lineNo, std::move(message)))};
    return result;
}

} // namespace

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

