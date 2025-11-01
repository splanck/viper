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
#include "il/io/ParserState.hpp"
#include "il/io/ParserUtil.hpp"
#include "il/io/StringEscape.hpp"

#include <charconv>
#include <cctype>
#include <limits>
#include <optional>
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

bool equalsIgnoreCase(std::string_view value, std::string_view literal)
{
    if (value.size() != literal.size())
        return false;
    for (size_t i = 0; i < literal.size(); ++i)
    {
        const unsigned char lhs = static_cast<unsigned char>(value[i]);
        const unsigned char rhs = static_cast<unsigned char>(literal[i]);
        if (std::tolower(lhs) != std::tolower(rhs))
            return false;
    }
    return true;
}

void skipSpace(std::string_view &text)
{
    size_t consumed = 0;
    while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed])))
        ++consumed;
    text.remove_prefix(consumed);
}

bool isIdentStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

bool isIdentBody(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$';
}

std::optional<std::string_view> parseIdent(std::string_view &text)
{
    std::string_view original = text;
    if (original.empty() || !isIdentStart(original.front()))
        return std::nullopt;

    size_t length = 1;
    while (length < original.size() && isIdentBody(original[length]))
        ++length;

    text.remove_prefix(length);
    return original.substr(0, length);
}

bool parseInt(std::string_view &text, int64_t &value)
{
    std::string_view original = text;
    if (original.empty())
        return false;

    const char *begin = original.data();
    const char *end = begin + original.size();
    auto [ptr, ec] = std::from_chars(begin, end, value, 10);
    if (ec != std::errc{})
        return false;
    text.remove_prefix(static_cast<size_t>(ptr - begin));
    return true;
}

bool parseBracketed(std::string_view &text, std::string_view &out)
{
    std::string_view original = text;
    if (original.empty() || original.front() != '[')
        return false;

    size_t depth = 0;
    size_t start = 0;
    bool inString = false;
    bool escape = false;
    for (size_t index = 0; index < original.size(); ++index)
    {
        char c = original[index];
        if (inString)
        {
            if (escape)
            {
                escape = false;
                continue;
            }
            if (c == '\\')
            {
                escape = true;
                continue;
            }
            if (c == '"')
                inString = false;
            continue;
        }

        if (c == '"')
        {
            inString = true;
            continue;
        }

        if (c == '[')
        {
            if (depth == 0)
                start = index + 1;
            ++depth;
            continue;
        }

        if (c == ']')
        {
            if (depth == 0)
                return false;
            --depth;
            if (depth == 0)
            {
                out = original.substr(start, index - start);
                text.remove_prefix(index + 1);
                return true;
            }
            continue;
        }
    }
    return false;
}

class OperandReader
{
  public:
    explicit OperandReader(ParserState &state) : state_(state) {}

    Expected<size_t> parseOperand(std::string_view &text, Value &out) const
    {
        bool matched = false;
        auto reg = tryParseRegister(text, out, matched);
        if (matched)
        {
            if (!reg)
                return reg;
            text.remove_prefix(reg.value());
            return reg;
        }

        auto mem = tryParseMemory(text, out, matched);
        if (matched)
        {
            if (!mem)
                return mem;
            text.remove_prefix(mem.value());
            return mem;
        }

        auto imm = parseImmediate(text, out);
        if (!imm)
            return imm;
        text.remove_prefix(imm.value());
        return imm;
    }

  private:
    Expected<size_t> tryParseRegister(std::string_view text, Value &out, bool &matched) const
    {
        matched = false;
        if (text.empty() || text.front() != '%')
            return Expected<size_t>{size_t{0}};

        matched = true;
        text.remove_prefix(1);
        auto identText = text;
        auto ident = parseIdent(identText);
        if (!ident || ident->empty())
            return makeSyntaxError<size_t>(state_, "missing temp name");

        std::string name(ident->begin(), ident->end());
        auto it = state_.tempIds.find(name);
        if (it != state_.tempIds.end())
        {
            out = Value::temp(it->second);
            return Expected<size_t>{1 + ident->size()};
        }

        if (name.size() > 1 && name.front() == 't')
        {
            std::string_view digits = name;
            digits.remove_prefix(1);
            std::string_view digitCursor = digits;
            int64_t parsed = 0;
            if (parseInt(digitCursor, parsed) && digitCursor.empty() && parsed >= 0 &&
                static_cast<uint64_t>(parsed) <= std::numeric_limits<unsigned>::max())
            {
                out = Value::temp(static_cast<unsigned>(parsed));
                return Expected<size_t>{1 + ident->size()};
            }
        }

        std::ostringstream oss;
        oss << "unknown temp '%" << name << "'";
        return makeSyntaxError<size_t>(state_, oss.str());
    }

    Expected<size_t> tryParseMemory(std::string_view text, Value &out, bool &matched) const
    {
        matched = false;
        if (text.empty() || text.front() != '[')
            return Expected<size_t>{size_t{0}};

        matched = true;
        std::string_view contents;
        auto cursor = text;
        if (!parseBracketed(cursor, contents))
            return makeSyntaxError<size_t>(state_, "unterminated memory operand");

        std::ostringstream oss;
        oss << "unsupported memory operand '[" << contents << "]'";
        return makeSyntaxError<size_t>(state_, oss.str());
    }

    Expected<size_t> parseImmediate(std::string_view text, Value &out) const
    {
        if (text.empty())
            return makeSyntaxError<size_t>(state_, "missing operand");

        std::string token(text);
        if (equalsIgnoreCase(token, "true"))
        {
            out = Value::constBool(true);
            return Expected<size_t>{token.size()};
        }
        if (equalsIgnoreCase(token, "false"))
        {
            out = Value::constBool(false);
            return Expected<size_t>{token.size()};
        }
        if (token == "null")
        {
            out = Value::null();
            return Expected<size_t>{token.size()};
        }
        if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        {
            std::string literal = token.substr(1, token.size() - 2);
            std::string decoded;
            std::string errMsg;
            if (!::il::io::decodeEscapedString(literal, decoded, &errMsg))
                return makeSyntaxError<size_t>(state_, std::move(errMsg));
            out = Value::constStr(std::move(decoded));
            return Expected<size_t>{token.size()};
        }

        const bool hasDecimalPoint = token.find('.') != std::string::npos;
        const bool isHexLiteral =
            token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X');
        const bool hasExponent = (!isHexLiteral) &&
                                 (token.find('e') != std::string::npos || token.find('E') != std::string::npos);

        auto parseFloatingToken = [&](const std::string &literal) -> Expected<size_t>
        {
            double value = 0.0;
            if (::il::io::parseFloatLiteral(literal, value))
            {
                out = Value::constFloat(value);
                return Expected<size_t>{literal.size()};
            }
            std::ostringstream oss;
            oss << "invalid floating literal '" << literal << "'";
            return makeSyntaxError<size_t>(state_, oss.str());
        };

        if (hasDecimalPoint || hasExponent || equalsIgnoreCase(token, "nan") ||
            equalsIgnoreCase(token, "inf") || equalsIgnoreCase(token, "+inf") ||
            equalsIgnoreCase(token, "-inf"))
        {
            return parseFloatingToken(token);
        }

        long long value = 0;
        if (::il::io::parseIntegerLiteral(token, value))
        {
            out = Value::constInt(value);
            return Expected<size_t>{token.size()};
        }

        std::ostringstream oss;
        oss << "invalid integer literal '" << token << "'";
        return makeSyntaxError<size_t>(state_, oss.str());
    }

    ParserState &state_;
};

Expected<Value> parseSymbolRef(std::string_view &text, Context &ctx)
{
    skipSpace(text);
    if (text.empty() || text.front() != '@')
        return Expected<Value>{makeError(ctx.state.curLoc, formatLineMessage(ctx, "missing global name"))};

    text.remove_prefix(1);
    auto nameCursor = text;
    auto ident = parseIdent(nameCursor);
    if (!ident || ident->empty())
        return Expected<Value>{makeError(ctx.state.curLoc, formatLineMessage(ctx, "missing global name"))};

    skipSpace(nameCursor);
    if (!nameCursor.empty())
        return Expected<Value>{makeError(ctx.state.curLoc, formatLineMessage(ctx, "malformed global name"))};

    text = nameCursor;
    return Value::global(std::string(ident->begin(), ident->end()));
}

} // namespace

ParseResult parseValueOperand(Cursor &cur, Context &ctx)
{
    std::string_view remaining = cur.remaining();
    skipSpace(remaining);
    if (remaining.empty())
        return syntaxError(ctx, "missing operand");

    if (remaining.front() == '@')
    {
        auto symbol = parseSymbolRef(remaining, ctx);
        if (!symbol)
        {
            ParseResult result;
            result.status = ::il::support::Expected<void>{symbol.error()};
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

    OperandReader reader(ctx.state);
    Value operand;
    auto consumed = reader.parseOperand(remaining, operand);
    if (!consumed)
    {
        ParseResult result;
        result.status = ::il::support::Expected<void>{consumed.error()};
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

