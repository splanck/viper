//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/OperandParse_ValueDetail.cpp
// Purpose: Provide reusable helpers backing the general value operand parser.
// Key invariants: Matches legacy OperandParser behaviour for temporaries,
//                 memory operands, and literal forwarding.
// Ownership/Lifetime: Operates on parser-managed state; no ownership transfer.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements support routines shared by the value operand helper.
/// @details This file contains the identifier scanning and operand classification
///          logic extracted from the legacy OperandParser, allowing the
///          high-level helper to remain compact while preserving diagnostic
///          fidelity.

#include "viper/il/io/OperandParse.hpp"

#include "il/core/Value.hpp"
#include "il/internal/io/ParserState.hpp"
#include "il/internal/io/ParserUtil.hpp"

#include "support/diag_expected.hpp"

#include <charconv>
#include <cctype>
#include <limits>
#include <optional>
#include <sstream>
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

template <class T> Expected<T> makeSyntaxError(ParserState &state, std::string message)
{
    return Expected<T>{makeError(state.curLoc, ::il::io::formatLineDiag(state.lineNo, std::move(message)))};
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

Expected<size_t> tryParseRegister(std::string_view text, Value &out, Context &ctx, bool &matched)
{
    matched = false;
    if (text.empty() || text.front() != '%')
        return Expected<size_t>{size_t{0}};

    matched = true;
    text.remove_prefix(1);
    auto identText = text;
    auto ident = parseIdent(identText);
    if (!ident || ident->empty())
        return makeSyntaxError<size_t>(ctx.state, "missing temp name");

    std::string name(ident->begin(), ident->end());
    auto it = ctx.state.tempIds.find(name);
    if (it != ctx.state.tempIds.end())
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
    return makeSyntaxError<size_t>(ctx.state, oss.str());
}

Expected<size_t> tryParseMemory(std::string_view text, Context &ctx, bool &matched)
{
    matched = false;
    if (text.empty() || text.front() != '[')
        return Expected<size_t>{size_t{0}};

    matched = true;
    std::string_view contents;
    auto cursor = text;
    if (!parseBracketed(cursor, contents))
        return makeSyntaxError<size_t>(ctx.state, "unterminated memory operand");

    std::ostringstream oss;
    oss << "unsupported memory operand '[" << contents << "]'";
    return makeSyntaxError<size_t>(ctx.state, oss.str());
}

Expected<size_t> parseImmediate(std::string_view text, Value &out, Context &ctx)
{
    viper::parse::Cursor literalCursor{text, viper::parse::SourcePos{ctx.state.lineNo, 0}};
    auto parsed = parseConstOperand(literalCursor, ctx);
    if (!parsed.ok())
        return Expected<size_t>{parsed.status.error()};
    if (!parsed.hasValue())
        return makeSyntaxError<size_t>(ctx.state, "missing operand");
    out = std::move(*parsed.value);
    return Expected<size_t>{literalCursor.offset()};
}

} // namespace

Expected<Value> parseSymbolOperand(std::string_view &text, Context &ctx)
{
    auto working = text;
    while (!working.empty() && std::isspace(static_cast<unsigned char>(working.front())))
        working.remove_prefix(1);
    if (working.empty() || working.front() != '@')
        return Expected<Value>{makeError(ctx.state.curLoc, ::il::io::formatLineDiag(ctx.state.lineNo, "missing global name"))};

    working.remove_prefix(1);
    auto identCursor = working;
    auto ident = parseIdent(identCursor);
    if (!ident || ident->empty())
        return Expected<Value>{makeError(ctx.state.curLoc, ::il::io::formatLineDiag(ctx.state.lineNo, "missing global name"))};

    while (!identCursor.empty() && std::isspace(static_cast<unsigned char>(identCursor.front())))
        identCursor.remove_prefix(1);
    if (!identCursor.empty())
        return Expected<Value>{makeError(ctx.state.curLoc, ::il::io::formatLineDiag(ctx.state.lineNo, "malformed global name"))};

    text = identCursor;
    return Value::global(std::string(ident->begin(), ident->end()));
}

Expected<size_t> parseValueTokenComponents(std::string_view &text, Value &out, Context &ctx)
{
    bool matched = false;
    auto reg = tryParseRegister(text, out, ctx, matched);
    if (matched)
    {
        if (!reg)
            return reg;
        text.remove_prefix(reg.value());
        return reg;
    }

    auto mem = tryParseMemory(text, ctx, matched);
    if (matched)
    {
        if (!mem)
            return mem;
        text.remove_prefix(mem.value());
        return mem;
    }

    auto imm = parseImmediate(text, out, ctx);
    if (!imm)
        return imm;
    text.remove_prefix(imm.value());
    return imm;
}

} // namespace viper::il::io

