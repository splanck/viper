//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Concentrates the character-level scanning helpers required by the textual IL
// operand parser.  The routines mirror the historical OperandParser behaviour so
// that temporary numbering, identifier acceptance, and diagnostics remain
// byte-for-byte compatible.  Keeping this glue in a dedicated translation unit
// allows the outward-facing helpers to stay compact while still surfacing rich
// syntax errors when operands are malformed.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the low-level helpers behind value operand parsing.
/// @details Provides identifier scanning, register detection, bracket matching,
///          and literal forwarding utilities that operate directly on
///          @c std::string_view slices.  Each helper mutates the provided view to
///          advance the caller's cursor while reporting errors via @ref Expected
///          so the surrounding parser can continue after recoverable mistakes.

#include "viper/il/io/OperandParse.hpp"

#include "il/core/Value.hpp"
#include "il/internal/io/ParserState.hpp"
#include "il/internal/io/ParserUtil.hpp"

#include "support/diag_expected.hpp"

#include <cctype>
#include <charconv>
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

/// @brief Construct an @ref Expected payload that reports a syntax error.
/// @details Formats @p message alongside the current line information stored in
///          @p state and returns it as an error-valued @ref Expected.  Centralising
///          the logic keeps diagnostic wording consistent across all helpers.
/// @tparam T Expected payload type.
/// @param state Parser bookkeeping providing the line number and cursor location.
/// @param message Human-readable explanation of the syntax issue encountered.
/// @return Error-valued @ref Expected propagating the formatted diagnostic.
template <class T> Expected<T> makeSyntaxError(ParserState &state, std::string message)
{
    return Expected<T>{::il::io::makeLineErrorDiag(state.curLoc, state.lineNo, std::move(message))};
}

/// @brief Check whether a character can start an identifier.
/// @details Mirrors the BASIC textual rules: alphabetic characters, underscores,
///          and dots are permitted so qualified lowering names remain valid.
/// @param c Character to test.
/// @return @c true when the character can appear at the start of an identifier.
bool isIdentStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

/// @brief Check whether a character can continue an identifier body.
/// @details Accepts the same characters as @ref isIdentStart plus digits and
///          BASIC type suffix characters ($ for string, # for double, % for
///          integer), preserving the classic BASIC type suffix syntax.
/// @param c Character to test.
/// @return @c true when the character is valid within an identifier body.
bool isIdentBody(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$' || c == '#' ||
           c == '%';
}

/// @brief Consume an identifier from the front of a string view.
/// @details Validates the first character using @ref isIdentStart before
///          consuming all subsequent body characters.  On success the helper
///          trims @p text to point at the character following the identifier and
///          returns the view referencing the consumed range.
/// @param text [in,out] Buffer containing the identifier to parse.
/// @return Identifier slice or @c std::nullopt when no identifier is present.
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

/// @brief Parse a signed decimal integer from the beginning of a string view.
/// @details Relies on @c std::from_chars for allocation-free conversion.  When
///          parsing succeeds the consumed characters are removed from @p text and
///          the numeric result is written to @p value.
/// @param text [in,out] Buffer containing the candidate integer literal.
/// @param value Destination for the parsed numeric value.
/// @return @c true when a decimal integer was consumed successfully.
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

/// @brief Extract the contents of a bracketed expression.
/// @details Walks the string while tracking nesting depth and quoted string
///          regions so nested brackets and escapes are handled correctly.  When
///          the outermost closing bracket is found the helper writes the
///          interior substring to @p out and advances @p text to the next
///          character after the closing bracket.
/// @param text [in,out] Source beginning with a '[' character.
/// @param out Output view referencing the characters inside the brackets.
/// @return @c true when a balanced bracketed region was consumed.
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

/// @brief Attempt to parse a temporary register operand.
/// @details Recognises the `%temp` syntax used by the serializer.  Names are
///          resolved through the temporary identifier table stored in @p ctx,
///          falling back to numeric encodings such as `%t4` when necessary to
///          preserve compatibility with older textual dumps.  On success the
///          helper writes the resolved value into @p out and reports the number
///          of characters consumed.  Syntax errors are returned via
///          @ref makeSyntaxError so diagnostics carry precise line information.
/// @param text Candidate substring beginning at the prospective `%` marker.
/// @param out [out] Destination receiving the resolved temporary value.
/// @param ctx Parser context exposing the known temporary identifiers.
/// @param matched [out] Indicates whether the `%` prefix was present.
/// @return Character count consumed on success or a diagnostic on failure.
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

/// @brief Attempt to parse a bracketed memory operand.
/// @details The textual IL currently rejects memory operands, but the helper
///          still consumes the bracketed syntax so it can emit a diagnostic that
///          matches the legacy parser.  Balanced bracket handling is delegated
///          to @ref parseBracketed.
/// @param text Candidate substring that may start with '['.
/// @param ctx Parser context used to format diagnostics.
/// @param matched [out] Set to @c true when a bracket prefix was recognised.
/// @return Character count consumed (unused) or a diagnostic when malformed.
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

/// @brief Parse an immediate literal operand.
/// @details Wraps the string slice in a @ref viper::parse::Cursor so it can
///          reuse @ref parseConstOperand, ensuring literal handling matches the
///          constant parser used elsewhere.  The resulting cursor offset reveals
///          how many characters were consumed and is returned to the caller.
/// @param text Candidate substring containing the literal text.
/// @param out [out] Resulting @ref Value when parsing succeeds.
/// @param ctx Parser context providing literal parsing utilities.
/// @return Characters consumed or a diagnostic on failure.
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

/// @brief Parse a global symbol operand using the `@name` notation.
/// @details Strips leading whitespace, requires an at-sign, and then delegates to
///          @ref parseIdent so qualified names and suffixes are handled
///          consistently.  Trailing non-whitespace characters trigger a
///          diagnostic to prevent ambiguous parsing when additional tokens follow
///          without spacing.
/// @param text [in,out] Buffer beginning at the potential symbol reference.
/// @param ctx Parser context exposing diagnostic helpers.
/// @return Parsed @ref Value or an error-valued @ref Expected when malformed.
Expected<Value> parseSymbolOperand(std::string_view &text, Context &ctx)
{
    auto working = text;
    while (!working.empty() && std::isspace(static_cast<unsigned char>(working.front())))
        working.remove_prefix(1);
    if (working.empty() || working.front() != '@')
        return Expected<Value>{
            ::il::io::makeLineErrorDiag(ctx.state.curLoc, ctx.state.lineNo, "missing global name")};

    working.remove_prefix(1);
    auto identCursor = working;
    auto ident = parseIdent(identCursor);
    if (!ident || ident->empty())
        return Expected<Value>{
            ::il::io::makeLineErrorDiag(ctx.state.curLoc, ctx.state.lineNo, "missing global name")};

    while (!identCursor.empty() && std::isspace(static_cast<unsigned char>(identCursor.front())))
        identCursor.remove_prefix(1);
    if (!identCursor.empty())
        return Expected<Value>{::il::io::makeLineErrorDiag(
            ctx.state.curLoc, ctx.state.lineNo, "malformed global name")};

    text = identCursor;
    return Value::global(std::string(ident->begin(), ident->end()));
}

/// @brief Parse the next operand fragment from a value token.
/// @details Attempts register, memory, and immediate parsing in that order,
///          updating @p text to begin at the character following the consumed
///          operand.  The helper mirrors the legacy parser's control flow so
///          existing diagnostic expectations remain valid.
/// @param text [in,out] Source text at the current operand boundary.
/// @param out [out] Resulting value when a register or literal is parsed.
/// @param ctx Parser context supplying lookup tables and diagnostics.
/// @return Characters consumed or a diagnostic when parsing fails.
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
