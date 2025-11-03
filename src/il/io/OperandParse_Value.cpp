//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/OperandParse_Value.cpp
// Purpose: Provide the per-kind parser for IL value operands.
// Key invariants: Mirrors OperandParser::parseValueToken behaviour.
// Ownership/Lifetime: Operates on parser-managed state without owning data.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the helper that parses general Value operands.
/// @details The implementation is a direct extraction of the legacy
///          OperandParser logic so diagnostics, whitespace handling, and literal
///          forms remain byte-for-byte compatible during the refactor.

#include "viper/il/io/OperandParse.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Value.hpp"
#include "il/internal/io/ParserState.hpp"
#include "il/internal/io/ParserUtil.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace viper::il::io
{
namespace
{
using ::il::core::Value;
using ::il::io::detail::ParserState;
using ::il::support::Expected;
using ::il::support::makeError;
using viper::parse::Cursor;

std::string formatLineMessage(Context &ctx, std::string message)
{
    return ::il::io::formatLineDiag(ctx.state.lineNo, std::move(message));
}

template <class T> Expected<T> makeSyntaxError(ParserState &state, std::string message)
{
    return Expected<T>{makeError(state.curLoc, ::il::io::formatLineDiag(state.lineNo, std::move(message)))};
}

ParseResult syntaxError(Context &ctx, std::string message)
{
    ParseResult result;
    result.status = ::il::support::Expected<void>{makeError(ctx.state.curLoc, formatLineMessage(ctx, std::move(message)))};
    return result;
}

void skipSpace(std::string_view &text)
{
    size_t consumed = 0;
    while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed])))
        ++consumed;
    text.remove_prefix(consumed);
}

} // namespace

Expected<size_t> parseValueTokenComponents(std::string_view &text, Value &out, Context &ctx);
Expected<Value> parseSymbolOperand(std::string_view &text, Context &ctx);

ParseResult parseValueOperand(Cursor &cur, Context &ctx)
{
    std::string_view remaining = cur.remaining();
    skipSpace(remaining);
    if (remaining.empty())
        return syntaxError(ctx, "missing operand");

    if (remaining.front() == '@')
    {
        auto symbol = parseSymbolOperand(remaining, ctx);
        if (!symbol)
        {
            ParseResult result;
            result.status = ::il::support::Expected<void>(symbol.error());
            return result;
        }

        skipSpace(remaining);
        if (!remaining.empty())
            return syntaxError(ctx, "unexpected trailing characters");

        cur.consumeRest();
        ParseResult result;
        result.value = std::move(symbol.value());
        return result;
    }

    Value operand;
    auto consumed = parseValueTokenComponents(remaining, operand, ctx);
    if (!consumed)
    {
        ParseResult result;
        result.status = ::il::support::Expected<void>(consumed.error());
        return result;
    }

    skipSpace(remaining);
    if (!remaining.empty())
        return syntaxError(ctx, "unexpected trailing characters");

    cur.consumeRest();
    ParseResult result;
    result.value = std::move(operand);
    return result;
}

} // namespace viper::il::io

