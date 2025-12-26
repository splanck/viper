//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/OperandParse_Type.cpp
// Purpose: Provide the per-kind parser for IL type immediates.
// Key invariants: Emits diagnostics consistent with the legacy OperandParser
//                 implementation while updating the instruction type in place.
// Ownership/Lifetime: Operates on parser-managed state without owning data and
//                     never allocates persistent resources.
// Links: docs/il-guide.md#reference and docs/il-reference.md#types
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the helper that parses type literal operands.
/// @details The helper mirrors the historical type parsing logic used by the
///          instruction parser, translating canonical textual spellings into
///          il::core::Type values and attaching them to the active instruction.

#include "viper/il/io/OperandParse.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/internal/io/ParserState.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "il/internal/io/TypeParser.hpp"

#include "support/diag_expected.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace viper::il::io
{
namespace
{

/// @brief Construct a parse result representing a type syntax error.
/// @details Type operand parsing uses the shared Expected-based diagnostic
///          channel.  This helper wraps the provided message with line/location
///          context, yielding a result whose status signals failure to the caller.
/// @param ctx Parser context holding the current source location.
/// @param message Diagnostic text to surface to the user.
/// @return Parse result whose status contains the formatted diagnostic.
ParseResult syntaxError(Context &ctx, std::string message)
{
    ParseResult result;
    result.status = ::il::support::Expected<void>{
        ::il::io::makeLineErrorDiag(ctx.state.curLoc, ctx.state.lineNo, std::move(message))};
    return result;
}

} // namespace

/// @brief Parse a type literal operand and attach it to the active instruction.
/// @details Consumes the next non-whitespace token, normalises trailing commas
///          produced by operand lists, and dispatches to the shared type parser.
///          Successful parses update @ref Context::instr while failures route
///          descriptive diagnostics through @ref syntaxError.  The cursor is
///          rewound to immediately after the consumed token so subsequent parsing
///          continues in lockstep.
/// @param cur Cursor positioned at the start of the type token.
/// @param ctx Parser context providing access to instruction state and diagnostics.
/// @return Parse result signalling success or failure.
ParseResult parseTypeOperand(viper::parse::Cursor &cur, Context &ctx)
{
    cur.skipWs();
    const std::size_t beginOffset = cur.offset();
    const std::string_view rawToken =
        cur.consumeWhile([](char ch) { return !std::isspace(static_cast<unsigned char>(ch)); });

    if (rawToken.empty())
        return syntaxError(ctx, "missing type");

    std::string token(rawToken);
    if (!token.empty() && token.back() == ',')
        token.pop_back();

    token = ::il::io::trim(token);
    if (token.empty())
        return syntaxError(ctx, "missing type");

    bool ok = false;
    ::il::core::Type type = ::il::io::parseType(token, &ok);
    if (!ok)
    {
        std::ostringstream oss;
        oss << "unknown type '" << token << "'";
        return syntaxError(ctx, oss.str());
    }

    ctx.instr.type = type;

    // Position the cursor after the consumed token.
    cur.seek(beginOffset + rawToken.size());

    ParseResult result;
    return result;
}

} // namespace viper::il::io
