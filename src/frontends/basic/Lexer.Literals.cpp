//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lexer.Literals.cpp
// Purpose: Implement literal scanning helpers for the BASIC lexer, isolating the
//          handling of numeric and string tokenisation from the driver loop.
// Key invariants: Literal lexers always consume characters from the shared
//                 cursor (`pos_`, `line_`, `column_`) in a balanced fashion so
//                 the caller can continue scanning without resynchronisation.
// Ownership/Lifetime: The helpers operate on the lexer's borrowed source view
//                     and never allocate beyond temporary token buffers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lexer.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace il::frontends::basic
{
namespace
{
bool isDecimalDigit(char ch)
{
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

bool isHexDigit(char ch)
{
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

bool isTypeSuffix(char ch)
{
    return ch == '#' || ch == '!' || ch == '%' || ch == '&';
}

/// @brief Determine whether the upcoming characters form a hexadecimal float literal.
///
/// @param src Entire source buffer for the lexer.
/// @param pos Current cursor position immediately following the leading `0`.
/// @return True when a <tt>0x</tt>/<tt>0X</tt> hexadecimal float follows.
bool hasHexFloatTail(std::string_view src, std::size_t pos)
{
    if (pos >= src.size())
        return false;
    char prefix = src[pos];
    if (prefix != 'x' && prefix != 'X')
        return false;
    ++pos;
    bool sawDigit = false;
    while (pos < src.size() && isHexDigit(src[pos]))
    {
        sawDigit = true;
        ++pos;
    }
    bool sawDot = false;
    if (pos < src.size() && src[pos] == '.')
    {
        sawDot = true;
        ++pos;
        while (pos < src.size() && isHexDigit(src[pos]))
        {
            sawDigit = true;
            ++pos;
        }
    }
    bool sawExp = false;
    if (pos < src.size() && (src[pos] == 'p' || src[pos] == 'P'))
        sawExp = true;
    return sawDigit && (sawDot || sawExp);
}

} // namespace

Token Lexer::lexNumber()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string lexeme;

    const auto consumeInto = [&](auto predicate) {
        while (!eof() && predicate(peek()))
            lexeme.push_back(get());
    };

    const auto consumeChar = [&]() -> char {
        char c = get();
        lexeme.push_back(c);
        return c;
    };

    if (peek() == '.')
        consumeChar();

    consumeInto(isDecimalDigit);

    bool handledHexFloat = false;
    if (lexeme == "0" && hasHexFloatTail(src_, pos_))
    {
        handledHexFloat = true;
        consumeChar(); // consume 'x' or 'X'
        consumeInto(isHexDigit);
        if (!eof() && peek() == '.')
        {
            consumeChar();
            consumeInto(isHexDigit);
        }
        if (!eof() && (peek() == 'p' || peek() == 'P'))
        {
            consumeChar();
            if (!eof() && (peek() == '+' || peek() == '-'))
                consumeChar();
            consumeInto(isDecimalDigit);
        }
    }

    if (!handledHexFloat)
    {
        if (!lexeme.empty() && lexeme.front() != '.' && peek() == '.')
        {
            consumeChar();
            consumeInto(isDecimalDigit);
        }
        if (!eof() && (peek() == 'e' || peek() == 'E'))
        {
            consumeChar();
            if (!eof() && (peek() == '+' || peek() == '-'))
                consumeChar();
            consumeInto(isDecimalDigit);
        }
    }

    if (!eof() && isTypeSuffix(peek()))
        consumeChar();

    return {TokenKind::Number, lexeme, loc};
}

Token Lexer::lexString()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string lexeme;

    auto appendEscape = [&](std::string &out) {
        auto appendHexDigits = [&](int maxDigits) {
            for (int i = 0; i < maxDigits && !eof(); ++i)
            {
                char next = peek();
                if (!isHexDigit(next))
                    break;
                out.push_back(get());
            }
        };
        auto appendOctalDigits = [&]() {
            for (int i = 0; i < 2 && !eof(); ++i)
            {
                char next = peek();
                if (next < '0' || next > '7')
                    break;
                out.push_back(get());
            }
        };
        out.push_back('\\');
        if (eof())
            return;
        char kind = get();
        out.push_back(kind);
        if ((kind == 'u' || kind == 'U'))
        {
            if (!eof() && peek() == '{')
            {
                out.push_back(get());
                while (!eof() && peek() != '}')
                    out.push_back(get());
                if (peek() == '}')
                    out.push_back(get());
                return;
            }
            appendHexDigits(kind == 'u' ? 4 : 8);
            return;
        }
        if (kind == 'x' || kind == 'X')
        {
            appendHexDigits(2);
            return;
        }
        if (kind >= '0' && kind <= '7')
        {
            appendOctalDigits();
            return;
        }
    };

    get(); // consume opening quote
    while (!eof() && peek() != '"')
    {
        char c = get();
        if (c == '\\')
        {
            appendEscape(lexeme);
            continue;
        }
        lexeme.push_back(c);
    }
    if (peek() == '"')
        get();
    return {TokenKind::String, lexeme, loc};
}

} // namespace il::frontends::basic
