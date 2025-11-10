//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/OperandParse_Const.cpp
// Purpose: Provide the per-kind parser for constant literal operands.
// Key invariants: Preserves OperandParser diagnostics and literal handling
//                 semantics for integers, floats, booleans, null, and strings.
// Ownership/Lifetime: Operates on parser-managed state without owning data and
//                     never allocates process-global resources.
// Links: docs/il-guide.md#reference and docs/il-reference.md#literal-operands
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the helper that parses constant literal operands.
/// @details The helper mirrors the legacy literal decoding rules, including
///          support for numeric suffixes and escaped string payloads, producing
///          il::core::Value instances identical to the historical parser.

#include "viper/il/io/OperandParse.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Value.hpp"
#include "il/internal/io/ParserState.hpp"
#include "il/internal/io/ParserUtil.hpp"
#include "il/io/StringEscape.hpp"

#include "support/diag_expected.hpp"

#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace viper::il::io
{
namespace
{

/// @brief Compare ASCII strings without considering letter case.
/// @details Literal parsing must recognise canonical spellings like "INF" and
///          "Inf" regardless of how the user wrote them.  This helper lowers
///          characters manually instead of relying on locale-aware facilities so
///          behaviour remains deterministic across hosts.
/// @param value Source token extracted from the IL stream.
/// @param literal Reference literal to compare against.
/// @return @c true when both strings match ignoring ASCII case, otherwise @c false.
bool equalsIgnoreCase(std::string_view value, std::string_view literal)
{
    if (value.size() != literal.size())
        return false;
    for (std::size_t index = 0; index < literal.size(); ++index)
    {
        const unsigned char lhs = static_cast<unsigned char>(value[index]);
        const unsigned char rhs = static_cast<unsigned char>(literal[index]);
        if (std::tolower(lhs) != std::tolower(rhs))
            return false;
    }
    return true;
}

/// @brief Build a parse result representing a syntax error at the current cursor.
/// @details Constant operand parsing reports failures through the
///          `Expected<void>` stored inside @ref ParseResult.  This helper
///          packages @p message with the active source location so diagnostics
///          mirror the legacy operand parser.
/// @param ctx Parser state carrying diagnostic and location information.
/// @param message Human readable description of the error.
/// @return Parse result whose status holds the constructed diagnostic.
ParseResult syntaxError(Context &ctx, std::string message)
{
    ParseResult result;
    result.status = ::il::support::Expected<void>{::il::support::makeError(
        ctx.state.curLoc, ::il::io::formatLineDiag(ctx.state.lineNo, std::move(message)))};
    return result;
}

/// @brief Consume the next whitespace-delimited token from the IL cursor.
/// @details The cursor hands back a view of the consumed characters and the
///          caller is responsible for trimming trailing delimiters such as
///          commas.  Returning `std::nullopt` allows the caller to emit a tailored
///          diagnostic when the operand list unexpectedly ends.
/// @param cur Cursor positioned at the beginning of the token.
/// @return View representing the consumed token or `std::nullopt` when no bytes remain.
std::optional<std::string_view> consumeToken(viper::parse::Cursor &cur)
{
    cur.skipWs();
    const std::size_t begin = cur.offset();
    const std::string_view token =
        cur.consumeWhile([](char ch) { return !std::isspace(static_cast<unsigned char>(ch)); });
    if (token.empty())
        return std::nullopt;
    cur.seek(begin + token.size());
    return token;
}

/// @brief Decode a quoted string literal operand from the cursor.
/// @details Copies characters out of the cursor while tracking escape
///          sequences, then delegates to @ref ::il::io::decodeEscapedString to
///          expand escapes into their runtime form.  The helper updates the
///          cursor position so subsequent parsers resume at the first
///          unconsumed byte following the literal.
/// @param cur Cursor positioned at the opening quote.
/// @param ctx Parser context used for diagnostic emission.
/// @return Parse result whose value contains the decoded string on success.
ParseResult parseStringLiteral(viper::parse::Cursor &cur, Context &ctx)
{
    const std::size_t begin = cur.offset();
    cur.consume('"');
    std::string literal;
    bool escape = false;
    while (!cur.atEnd())
    {
        const char ch = cur.peek();
        cur.advance();
        if (escape)
        {
            literal.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\')
        {
            literal.push_back(ch);
            escape = true;
            continue;
        }
        literal.push_back(ch);
        if (ch == '"')
            break;
    }

    if (literal.empty() || literal.back() != '"')
        return syntaxError(ctx, "unterminated string literal");

    std::string payload = literal.substr(0, literal.size() - 1);
    std::string decoded;
    std::string err;
    if (!::il::io::decodeEscapedString(payload, decoded, &err))
        return syntaxError(ctx, err);

    ParseResult result;
    result.value = ::il::core::Value::constStr(std::move(decoded));
    cur.seek(begin + 1 + literal.size());
    return result;
}

/// @brief Interpret @p token as either an integer or floating-point literal.
/// @details Examines the token for decimal points, exponent markers, or
///          well-known floating spellings (INF/NAN) before dispatching to the
///          shared literal parsing helpers.  Diagnostics match the historical
///          operand parser so tools that diff output remain stable.
/// @param token Literal token stripped of trailing delimiters and whitespace.
/// @param ctx Parser context that receives diagnostics when parsing fails.
/// @return Parse result containing the parsed value or an error status.
ParseResult parseNumericLiteral(const std::string &token, Context &ctx)
{
    ParseResult result;

    const bool hasDecimalPoint = token.find('.') != std::string::npos;
    const bool isHexLiteral =
        token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X');
    const bool hasExponent = (!isHexLiteral) && (token.find('e') != std::string::npos ||
                                                 token.find('E') != std::string::npos);

    auto handleFloat = [&](const std::string &literal) -> ParseResult
    {
        double value = 0.0;
        if (::il::io::parseFloatLiteral(literal, value))
        {
            result.value = ::il::core::Value::constFloat(value);
            return result;
        }
        std::ostringstream oss;
        oss << "invalid floating literal '" << literal << "'";
        return syntaxError(ctx, oss.str());
    };

    if (hasDecimalPoint || hasExponent || equalsIgnoreCase(token, "nan") ||
        equalsIgnoreCase(token, "inf") || equalsIgnoreCase(token, "+inf") ||
        equalsIgnoreCase(token, "-inf"))
        return handleFloat(token);

    long long intValue = 0;
    if (::il::io::parseIntegerLiteral(token, intValue))
    {
        result.value = ::il::core::Value::constInt(intValue);
        return result;
    }

    std::ostringstream oss;
    oss << "invalid integer literal '" << token << "'";
    return syntaxError(ctx, oss.str());
}

} // namespace

/// @brief Parse a literal constant operand from the IL token stream.
/// @details Handles strings, booleans, `null`, and numeric literals while
///          trimming delimiter characters that separate operands.  Each case
///          delegates to a specialist helper to keep the control flow readable
///          and to reuse shared validation routines.  Diagnostics are emitted via
///          @ref syntaxError so the parser maintains consistent formatting.
/// @param cur Cursor describing the remaining operand text.
/// @param ctx Parser context capturing diagnostics and results.
/// @return Parse result containing the decoded literal or an error.
ParseResult parseConstOperand(viper::parse::Cursor &cur, Context &ctx)
{
    cur.skipWs();
    if (cur.atEnd())
        return syntaxError(ctx, "missing operand");

    if (cur.peek() == '"')
        return parseStringLiteral(cur, ctx);

    auto tokenView = consumeToken(cur);
    if (!tokenView)
        return syntaxError(ctx, "missing operand");

    std::string token(tokenView->begin(), tokenView->end());
    if (!token.empty() && (token.back() == ',' || token.back() == ')'))
    {
        char tail = token.back();
        token.pop_back();
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
            token.pop_back();
        if (token.empty())
        {
            std::string msg = tail == ',' ? "missing operand" : "missing operand";
            return syntaxError(ctx, msg);
        }
    }

    if (equalsIgnoreCase(token, "true"))
    {
        ParseResult result;
        result.value = ::il::core::Value::constBool(true);
        return result;
    }
    if (equalsIgnoreCase(token, "false"))
    {
        ParseResult result;
        result.value = ::il::core::Value::constBool(false);
        return result;
    }
    if (token == "null")
    {
        ParseResult result;
        result.value = ::il::core::Value::null();
        return result;
    }

    return parseNumericLiteral(token, ctx);
}

} // namespace viper::il::io
