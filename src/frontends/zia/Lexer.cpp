//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lexer.cpp
/// @brief Implementation of the Zia lexical analyzer.
///
/// @details This file implements the Lexer class which tokenizes Zia
/// source code. Key implementation details:
///
/// ## Keyword Lookup
///
/// Keywords are stored in a sorted array (kKeywordTable) for O(log n) binary
/// search lookup. The table contains 33 keywords from "as" to "while".
///
/// ## String Interpolation
///
/// Interpolated strings like `"Hello ${name}!"` are handled by:
/// 1. Returning StringStart token for `"Hello ${`
/// 2. Tracking interpolation depth and brace nesting
/// 3. Resuming string lexing after `}` to emit StringMid or StringEnd
///
/// ## Number Literals
///
/// Supports decimal, hexadecimal (0x), and binary (0b) integer literals,
/// plus floating-point with optional exponent (1.5e-3).
///
/// @see Lexer.hpp for the class interface
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lexer.hpp"
#include "frontends/common/CharUtils.hpp"
#include "frontends/common/NumberParsing.hpp"
#include <array>
#include <charconv>
#include <string_view>

namespace il::frontends::zia
{

//===----------------------------------------------------------------------===//
// TokenKind to string conversion
//===----------------------------------------------------------------------===//

const char *tokenKindToString(TokenKind kind)
{
    switch (kind)
    {
        case TokenKind::Eof:
            return "eof";
        case TokenKind::Error:
            return "error";
        case TokenKind::IntegerLiteral:
            return "integer";
        case TokenKind::NumberLiteral:
            return "number";
        case TokenKind::StringLiteral:
            return "string";
        case TokenKind::Identifier:
            return "identifier";
        case TokenKind::StringStart:
            return "string_start";
        case TokenKind::StringMid:
            return "string_mid";
        case TokenKind::StringEnd:
            return "string_end";

        // Keywords
        case TokenKind::KwValue:
            return "value";
        case TokenKind::KwEntity:
            return "entity";
        case TokenKind::KwInterface:
            return "interface";
        case TokenKind::KwFinal:
            return "final";
        case TokenKind::KwExpose:
            return "expose";
        case TokenKind::KwHide:
            return "hide";
        case TokenKind::KwOverride:
            return "override";
        case TokenKind::KwWeak:
            return "weak";
        case TokenKind::KwModule:
            return "module";
        case TokenKind::KwNamespace:
            return "namespace";
        case TokenKind::KwImport:
            return "import";
        case TokenKind::KwFunc:
            return "func";
        case TokenKind::KwReturn:
            return "return";
        case TokenKind::KwVar:
            return "var";
        case TokenKind::KwNew:
            return "new";
        case TokenKind::KwIf:
            return "if";
        case TokenKind::KwElse:
            return "else";
        case TokenKind::KwLet:
            return "let";
        case TokenKind::KwMatch:
            return "match";
        case TokenKind::KwWhile:
            return "while";
        case TokenKind::KwFor:
            return "for";
        case TokenKind::KwIn:
            return "in";
        case TokenKind::KwIs:
            return "is";
        case TokenKind::KwGuard:
            return "guard";
        case TokenKind::KwBreak:
            return "break";
        case TokenKind::KwContinue:
            return "continue";
        case TokenKind::KwExtends:
            return "extends";
        case TokenKind::KwImplements:
            return "implements";
        case TokenKind::KwSelf:
            return "self";
        case TokenKind::KwSuper:
            return "super";
        case TokenKind::KwAs:
            return "as";
        case TokenKind::KwTrue:
            return "true";
        case TokenKind::KwFalse:
            return "false";
        case TokenKind::KwNull:
            return "null";
        case TokenKind::KwAnd:
            return "and";
        case TokenKind::KwOr:
            return "or";
        case TokenKind::KwNot:
            return "not";

        // Operators
        case TokenKind::Plus:
            return "+";
        case TokenKind::Minus:
            return "-";
        case TokenKind::Star:
            return "*";
        case TokenKind::Slash:
            return "/";
        case TokenKind::Percent:
            return "%";
        case TokenKind::Ampersand:
            return "&";
        case TokenKind::Pipe:
            return "|";
        case TokenKind::Caret:
            return "^";
        case TokenKind::Tilde:
            return "~";
        case TokenKind::Bang:
            return "!";
        case TokenKind::Equal:
            return "=";
        case TokenKind::EqualEqual:
            return "==";
        case TokenKind::NotEqual:
            return "!=";
        case TokenKind::Less:
            return "<";
        case TokenKind::LessEqual:
            return "<=";
        case TokenKind::Greater:
            return ">";
        case TokenKind::GreaterEqual:
            return ">=";
        case TokenKind::AmpAmp:
            return "&&";
        case TokenKind::PipePipe:
            return "||";
        case TokenKind::Arrow:
            return "->";
        case TokenKind::FatArrow:
            return "=>";
        case TokenKind::Question:
            return "?";
        case TokenKind::QuestionQuestion:
            return "??";
        case TokenKind::QuestionDot:
            return "?.";
        case TokenKind::Dot:
            return ".";
        case TokenKind::DotDot:
            return "..";
        case TokenKind::DotDotEqual:
            return "..=";
        case TokenKind::Colon:
            return ":";
        case TokenKind::Semicolon:
            return ";";
        case TokenKind::Comma:
            return ",";
        case TokenKind::At:
            return "@";

        // Brackets
        case TokenKind::LParen:
            return "(";
        case TokenKind::RParen:
            return ")";
        case TokenKind::LBracket:
            return "[";
        case TokenKind::RBracket:
            return "]";
        case TokenKind::LBrace:
            return "{";
        case TokenKind::RBrace:
            return "}";
    }
    return "?";
}

bool Token::isKeyword() const
{
    return kind >= TokenKind::KwValue && kind <= TokenKind::KwNot;
}

//===----------------------------------------------------------------------===//
// Keyword lookup table
//===----------------------------------------------------------------------===//

namespace
{

struct KeywordEntry
{
    std::string_view key;
    TokenKind kind;
};

// Sorted for binary search (37 keywords)
constexpr std::array<KeywordEntry, 37> kKeywordTable = {{
    {"and", TokenKind::KwAnd},
    {"as", TokenKind::KwAs},
    {"break", TokenKind::KwBreak},
    {"continue", TokenKind::KwContinue},
    {"else", TokenKind::KwElse},
    {"entity", TokenKind::KwEntity},
    {"expose", TokenKind::KwExpose},
    {"extends", TokenKind::KwExtends},
    {"false", TokenKind::KwFalse},
    {"final", TokenKind::KwFinal},
    {"for", TokenKind::KwFor},
    {"func", TokenKind::KwFunc},
    {"guard", TokenKind::KwGuard},
    {"hide", TokenKind::KwHide},
    {"if", TokenKind::KwIf},
    {"implements", TokenKind::KwImplements},
    {"import", TokenKind::KwImport},
    {"in", TokenKind::KwIn},
    {"interface", TokenKind::KwInterface},
    {"is", TokenKind::KwIs},
    {"let", TokenKind::KwLet},
    {"match", TokenKind::KwMatch},
    {"module", TokenKind::KwModule},
    {"namespace", TokenKind::KwNamespace},
    {"new", TokenKind::KwNew},
    {"not", TokenKind::KwNot},
    {"null", TokenKind::KwNull},
    {"or", TokenKind::KwOr},
    {"override", TokenKind::KwOverride},
    {"return", TokenKind::KwReturn},
    {"self", TokenKind::KwSelf},
    {"super", TokenKind::KwSuper},
    {"true", TokenKind::KwTrue},
    {"value", TokenKind::KwValue},
    {"var", TokenKind::KwVar},
    {"weak", TokenKind::KwWeak},
    {"while", TokenKind::KwWhile},
}};

// Use common character utilities
using common::char_utils::isDigit;
using common::char_utils::isHexDigit;
using common::char_utils::isLetter;
using common::char_utils::isWhitespace;

/// @brief Check if character can start an identifier (letter or underscore).
inline bool isIdentifierStart(char c)
{
    return isLetter(c) || c == '_';
}

/// @brief Check if character can continue an identifier.
inline bool isIdentifierContinue(char c)
{
    return isLetter(c) || isDigit(c) || c == '_';
}

} // anonymous namespace

std::optional<TokenKind> Lexer::lookupKeyword(const std::string &name)
{
    auto it = std::lower_bound(kKeywordTable.begin(),
                               kKeywordTable.end(),
                               name,
                               [](const KeywordEntry &entry, const std::string &key)
                               { return entry.key < key; });
    if (it != kKeywordTable.end() && it->key == name)
        return it->kind;
    return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Lexer implementation
//===----------------------------------------------------------------------===//

Lexer::Lexer(std::string source, uint32_t fileId, il::support::DiagnosticEngine &diag)
    : source_(std::move(source)), fileId_(fileId), diag_(diag)
{
}

char Lexer::peekChar() const
{
    if (pos_ >= source_.size())
        return '\0';
    return source_[pos_];
}

char Lexer::peekChar(size_t offset) const
{
    if (pos_ + offset >= source_.size())
        return '\0';
    return source_[pos_ + offset];
}

char Lexer::getChar()
{
    if (pos_ >= source_.size())
        return '\0';
    char c = source_[pos_++];
    if (c == '\n')
    {
        ++line_;
        column_ = 1;
    }
    else
    {
        ++column_;
    }
    return c;
}

bool Lexer::eof() const
{
    return pos_ >= source_.size();
}

il::support::SourceLoc Lexer::currentLoc() const
{
    return il::support::SourceLoc{fileId_, line_, column_};
}

void Lexer::reportError(il::support::SourceLoc loc, const std::string &message)
{
    diag_.report(il::support::Diagnostic{
        il::support::Severity::Error,
        message,
        loc,
        "V1000" // Zia lexer error code
    });
}

void Lexer::skipLineComment()
{
    // Skip the //
    getChar();
    getChar();
    // Skip until end of line or EOF
    while (!eof() && peekChar() != '\n')
    {
        getChar();
    }
}

bool Lexer::skipBlockComment()
{
    il::support::SourceLoc startLoc = currentLoc();

    // Skip /*
    getChar();
    getChar();

    int depth = 1; // Support nested comments
    while (!eof() && depth > 0)
    {
        char c = getChar();
        if (c == '/' && peekChar() == '*')
        {
            getChar();
            ++depth;
        }
        else if (c == '*' && peekChar() == '/')
        {
            getChar();
            --depth;
        }
    }

    if (depth > 0)
    {
        reportError(startLoc, "unterminated block comment");
        return false;
    }
    return true;
}

void Lexer::skipWhitespaceAndComments()
{
    while (!eof())
    {
        char c = peekChar();

        if (isWhitespace(c))
        {
            getChar();
            continue;
        }

        // Line comment: //
        if (c == '/' && peekChar(1) == '/')
        {
            skipLineComment();
            continue;
        }

        // Block comment: /* ... */
        if (c == '/' && peekChar(1) == '*')
        {
            skipBlockComment();
            continue;
        }

        break;
    }
}

Token Lexer::lexIdentifierOrKeyword()
{
    Token tok;
    tok.loc = currentLoc();
    tok.text.reserve(16);

    // Consume identifier characters
    while (!eof() && isIdentifierContinue(peekChar()))
    {
        tok.text.push_back(getChar());
    }

    // Check if it's a keyword (case-sensitive)
    if (auto kw = lookupKeyword(tok.text))
    {
        tok.kind = *kw;
        return tok;
    }

    tok.kind = TokenKind::Identifier;
    return tok;
}

Token Lexer::lexNumber()
{
    Token tok;
    tok.loc = currentLoc();
    tok.kind = TokenKind::IntegerLiteral;

    // Check for hex (0x) or binary (0b)
    if (peekChar() == '0')
    {
        char next = peekChar(1);
        if (next == 'x' || next == 'X')
        {
            // Hex literal
            tok.text.push_back(getChar()); // '0'
            tok.text.push_back(getChar()); // 'x'

            if (!isHexDigit(peekChar()))
            {
                reportError(tok.loc, "invalid hex literal: expected hex digits after 0x");
                tok.kind = TokenKind::Error;
                return tok;
            }

            while (!eof() && isHexDigit(peekChar()))
            {
                tok.text.push_back(getChar());
            }

            // Parse hex value
            std::string_view hexDigits(tok.text.data() + 2, tok.text.size() - 2);
            auto parsed = common::number_parsing::parseHexLiteral(hexDigits);
            if (!parsed.valid)
            {
                if (parsed.overflow)
                    reportError(tok.loc, "hex literal out of range");
                else
                    reportError(tok.loc, "invalid hex literal");
                tok.kind = TokenKind::Error;
            }
            else
            {
                tok.intValue = parsed.intValue;
            }
            return tok;
        }
        else if (next == 'b' || next == 'B')
        {
            // Binary literal
            tok.text.push_back(getChar()); // '0'
            tok.text.push_back(getChar()); // 'b'

            if (peekChar() != '0' && peekChar() != '1')
            {
                reportError(tok.loc, "invalid binary literal: expected binary digits after 0b");
                tok.kind = TokenKind::Error;
                return tok;
            }

            while (!eof() && (peekChar() == '0' || peekChar() == '1'))
            {
                tok.text.push_back(getChar());
            }

            // Parse binary value
            uint64_t value = 0;
            for (size_t i = 2; i < tok.text.size(); ++i)
            {
                if (value > (UINT64_MAX >> 1))
                {
                    reportError(tok.loc, "binary literal out of range");
                    tok.kind = TokenKind::Error;
                    return tok;
                }
                value = (value << 1) | static_cast<uint64_t>(tok.text[i] - '0');
            }
            tok.intValue = static_cast<int64_t>(value);
            return tok;
        }
    }

    // Decimal number
    while (!eof() && isDigit(peekChar()))
    {
        tok.text.push_back(getChar());
    }

    // Check for decimal point (but not .. range operator)
    if (peekChar() == '.' && peekChar(1) != '.')
    {
        tok.kind = TokenKind::NumberLiteral;
        tok.text.push_back(getChar()); // consume '.'

        // Consume fractional part
        while (!eof() && isDigit(peekChar()))
        {
            tok.text.push_back(getChar());
        }
    }

    // Check for exponent
    char e = peekChar();
    if (e == 'e' || e == 'E')
    {
        tok.kind = TokenKind::NumberLiteral;
        tok.text.push_back(getChar()); // consume 'e' or 'E'

        // Optional sign
        char sign = peekChar();
        if (sign == '+' || sign == '-')
        {
            tok.text.push_back(getChar());
        }

        // Exponent digits
        if (!isDigit(peekChar()))
        {
            reportError(tok.loc, "invalid numeric literal: expected exponent digits");
            tok.kind = TokenKind::Error;
            return tok;
        }

        while (!eof() && isDigit(peekChar()))
        {
            tok.text.push_back(getChar());
        }
    }

    // Parse the value
    auto parsed = common::number_parsing::parseDecimalLiteral(tok.text);
    if (!parsed.valid)
    {
        if (parsed.overflow)
            reportError(tok.loc, "numeric literal out of range");
        else
            reportError(tok.loc, "invalid numeric literal");
        tok.kind = TokenKind::Error;
    }
    else if (parsed.isFloat)
    {
        tok.kind = TokenKind::NumberLiteral;
        tok.floatValue = parsed.floatValue;
    }
    else
    {
        tok.kind = TokenKind::IntegerLiteral;
        tok.intValue = parsed.intValue;
    }

    return tok;
}

std::optional<char> Lexer::processEscape(char c)
{
    switch (c)
    {
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case 'b':
            return '\b';
        case 'a':
            return '\a';
        case 'f':
            return '\f';
        case 'v':
            return '\v';
        case '\\':
            return '\\';
        case '"':
            return '"';
        case '\'':
            return '\'';
        case '0':
            return '\0';
        case '$':
            return '$'; // For string interpolation escape
        default:
            return std::nullopt;
    }
}

int Lexer::hexDigitValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::optional<std::string> Lexer::processUnicodeEscape()
{
    // Expects to be called after consuming \u
    // Reads 4 hex digits and returns the UTF-8 encoded character
    if (eof())
        return std::nullopt;

    uint32_t codepoint = 0;
    for (int i = 0; i < 4; i++)
    {
        if (eof())
            return std::nullopt;
        char c = peekChar();
        int val = hexDigitValue(c);
        if (val < 0)
            return std::nullopt;
        getChar(); // consume the hex digit
        codepoint = (codepoint << 4) | val;
    }

    // Convert codepoint to UTF-8
    std::string result;
    if (codepoint <= 0x7F)
    {
        result.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FF)
    {
        result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0xFFFF)
    {
        result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0x10FFFF)
    {
        result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        return std::nullopt; // Invalid codepoint
    }
    return result;
}

std::optional<char> Lexer::processHexEscape()
{
    // Expects to be called after consuming \x
    // Reads 2 hex digits and returns the character
    if (eof())
        return std::nullopt;

    int high = hexDigitValue(peekChar());
    if (high < 0)
        return std::nullopt;
    getChar();

    if (eof())
        return std::nullopt;

    int low = hexDigitValue(peekChar());
    if (low < 0)
        return std::nullopt;
    getChar();

    return static_cast<char>((high << 4) | low);
}

Token Lexer::lexString()
{
    Token tok;
    tok.loc = currentLoc();
    tok.kind = TokenKind::StringLiteral;

    // Check for triple-quoted string
    if (peekChar() == '"' && peekChar(1) == '"' && peekChar(2) == '"')
    {
        return lexTripleQuotedString();
    }

    tok.text.push_back(getChar()); // consume opening "

    while (!eof())
    {
        char c = peekChar();

        // Check for closing quote
        if (c == '"')
        {
            tok.text.push_back(getChar());
            return tok;
        }

        // Check for string interpolation: ${
        if (c == '$' && peekChar(1) == '{')
        {
            // This is an interpolated string - change token kind to StringStart
            tok.kind = TokenKind::StringStart;
            tok.text.push_back(getChar()); // consume '$'
            tok.text.push_back(getChar()); // consume '{'
            // Enter interpolation mode
            interpolationDepth_++;
            braceDepth_.push_back(0);
            return tok;
        }

        // Check for newline (error in single-quoted string)
        if (c == '\n' || c == '\r')
        {
            reportError(tok.loc, "newline in string literal");
            tok.kind = TokenKind::Error;
            return tok;
        }

        // Check for escape sequence
        if (c == '\\')
        {
            tok.text.push_back(getChar()); // consume '\'
            if (eof())
            {
                reportError(tok.loc, "unterminated escape sequence");
                tok.kind = TokenKind::Error;
                return tok;
            }
            char escaped = peekChar();
            tok.text.push_back(getChar()); // consume the escape character

            // Handle unicode escape \uXXXX
            if (escaped == 'u')
            {
                if (auto utf8 = processUnicodeEscape())
                {
                    for (char ch : *utf8)
                    {
                        tok.stringValue.push_back(ch);
                    }
                }
                else
                {
                    reportError(tok.loc, "invalid unicode escape sequence: expected \\uXXXX");
                }
                continue;
            }

            // Handle hex escape \xXX
            if (escaped == 'x')
            {
                if (auto hexChar = processHexEscape())
                {
                    tok.stringValue.push_back(*hexChar);
                }
                else
                {
                    reportError(tok.loc, "invalid hex escape sequence: expected \\xXX");
                }
                continue;
            }

            // Handle simple escape sequences
            if (auto esc = processEscape(escaped))
            {
                tok.stringValue.push_back(*esc);
            }
            else
            {
                reportError(tok.loc, std::string("invalid escape sequence: \\") + escaped);
            }
            continue;
        }

        tok.text.push_back(getChar());
        tok.stringValue.push_back(c);
    }

    reportError(tok.loc, "unterminated string literal");
    tok.kind = TokenKind::Error;
    return tok;
}

Token Lexer::lexInterpolatedStringContinuation()
{
    Token tok;
    tok.loc = currentLoc();

    // We just consumed '}' - now continue reading the string
    while (!eof())
    {
        char c = peekChar();

        // Check for closing quote
        if (c == '"')
        {
            tok.text.push_back(getChar());
            tok.kind = TokenKind::StringEnd;
            return tok;
        }

        // Check for another interpolation: ${
        if (c == '$' && peekChar(1) == '{')
        {
            tok.kind = TokenKind::StringMid;
            tok.text.push_back(getChar()); // consume '$'
            tok.text.push_back(getChar()); // consume '{'
            // Stay in interpolation mode but reset brace depth for this level
            braceDepth_.back() = 0;
            return tok;
        }

        // Check for newline (error in string)
        if (c == '\n' || c == '\r')
        {
            reportError(tok.loc, "newline in string literal");
            tok.kind = TokenKind::Error;
            return tok;
        }

        // Check for escape sequence
        if (c == '\\')
        {
            tok.text.push_back(getChar()); // consume '\'
            if (eof())
            {
                reportError(tok.loc, "unterminated escape sequence");
                tok.kind = TokenKind::Error;
                return tok;
            }
            char escaped = peekChar();
            tok.text.push_back(getChar()); // consume the escape character
            if (auto esc = processEscape(escaped))
            {
                tok.stringValue.push_back(*esc);
            }
            else
            {
                reportError(tok.loc, std::string("invalid escape sequence: \\") + escaped);
            }
            continue;
        }

        tok.text.push_back(getChar());
        tok.stringValue.push_back(c);
    }

    reportError(tok.loc, "unterminated interpolated string");
    tok.kind = TokenKind::Error;
    return tok;
}

Token Lexer::lexTripleQuotedString()
{
    Token tok;
    tok.loc = currentLoc();
    tok.kind = TokenKind::StringLiteral;

    // Consume opening """
    tok.text.push_back(getChar());
    tok.text.push_back(getChar());
    tok.text.push_back(getChar());

    while (!eof())
    {
        char c = peekChar();

        // Check for closing """
        if (c == '"' && peekChar(1) == '"' && peekChar(2) == '"')
        {
            tok.text.push_back(getChar());
            tok.text.push_back(getChar());
            tok.text.push_back(getChar());
            return tok;
        }

        // Handle escape sequences
        if (c == '\\')
        {
            tok.text.push_back(getChar()); // consume '\'
            if (!eof())
            {
                char escaped = peekChar();
                tok.text.push_back(getChar()); // consume the escape character
                if (auto esc = processEscape(escaped))
                {
                    tok.stringValue.push_back(*esc);
                }
                else
                {
                    // In triple-quoted, just preserve the backslash
                    tok.stringValue.push_back('\\');
                    tok.stringValue.push_back(escaped);
                }
            }
            continue;
        }

        tok.text.push_back(getChar());
        tok.stringValue.push_back(c);
    }

    reportError(tok.loc, "unterminated triple-quoted string");
    tok.kind = TokenKind::Error;
    return tok;
}

Token Lexer::next()
{
    // Return cached token if available
    if (peeked_.has_value())
    {
        Token tok = std::move(*peeked_);
        peeked_.reset();
        return tok;
    }

    skipWhitespaceAndComments();

    if (eof())
    {
        Token tok;
        tok.kind = TokenKind::Eof;
        tok.loc = currentLoc();
        return tok;
    }

    char c = peekChar();

    // Identifier or keyword
    if (isIdentifierStart(c))
    {
        return lexIdentifierOrKeyword();
    }

    // Number
    if (isDigit(c))
    {
        return lexNumber();
    }

    // String literal
    if (c == '"')
    {
        return lexString();
    }

    // Operators and punctuation
    Token tok;
    tok.loc = currentLoc();

    switch (c)
    {
        case '+':
            tok.kind = TokenKind::Plus;
            tok.text = "+";
            getChar();
            break;

        case '-':
            getChar();
            if (peekChar() == '>')
            {
                getChar();
                tok.kind = TokenKind::Arrow;
                tok.text = "->";
            }
            else
            {
                tok.kind = TokenKind::Minus;
                tok.text = "-";
            }
            break;

        case '*':
            tok.kind = TokenKind::Star;
            tok.text = "*";
            getChar();
            break;

        case '/':
            tok.kind = TokenKind::Slash;
            tok.text = "/";
            getChar();
            break;

        case '%':
            tok.kind = TokenKind::Percent;
            tok.text = "%";
            getChar();
            break;

        case '&':
            getChar();
            if (peekChar() == '&')
            {
                getChar();
                tok.kind = TokenKind::AmpAmp;
                tok.text = "&&";
            }
            else
            {
                tok.kind = TokenKind::Ampersand;
                tok.text = "&";
            }
            break;

        case '|':
            getChar();
            if (peekChar() == '|')
            {
                getChar();
                tok.kind = TokenKind::PipePipe;
                tok.text = "||";
            }
            else
            {
                tok.kind = TokenKind::Pipe;
                tok.text = "|";
            }
            break;

        case '^':
            tok.kind = TokenKind::Caret;
            tok.text = "^";
            getChar();
            break;

        case '~':
            tok.kind = TokenKind::Tilde;
            tok.text = "~";
            getChar();
            break;

        case '!':
            getChar();
            if (peekChar() == '=')
            {
                getChar();
                tok.kind = TokenKind::NotEqual;
                tok.text = "!=";
            }
            else
            {
                tok.kind = TokenKind::Bang;
                tok.text = "!";
            }
            break;

        case '=':
            getChar();
            if (peekChar() == '=')
            {
                getChar();
                tok.kind = TokenKind::EqualEqual;
                tok.text = "==";
            }
            else if (peekChar() == '>')
            {
                getChar();
                tok.kind = TokenKind::FatArrow;
                tok.text = "=>";
            }
            else
            {
                tok.kind = TokenKind::Equal;
                tok.text = "=";
            }
            break;

        case '<':
            getChar();
            if (peekChar() == '=')
            {
                getChar();
                tok.kind = TokenKind::LessEqual;
                tok.text = "<=";
            }
            else
            {
                tok.kind = TokenKind::Less;
                tok.text = "<";
            }
            break;

        case '>':
            getChar();
            if (peekChar() == '=')
            {
                getChar();
                tok.kind = TokenKind::GreaterEqual;
                tok.text = ">=";
            }
            else
            {
                tok.kind = TokenKind::Greater;
                tok.text = ">";
            }
            break;

        case '?':
            getChar();
            if (peekChar() == '?')
            {
                getChar();
                tok.kind = TokenKind::QuestionQuestion;
                tok.text = "??";
            }
            else if (peekChar() == '.')
            {
                getChar();
                tok.kind = TokenKind::QuestionDot;
                tok.text = "?.";
            }
            else
            {
                tok.kind = TokenKind::Question;
                tok.text = "?";
            }
            break;

        case '.':
            getChar();
            if (peekChar() == '.')
            {
                getChar();
                if (peekChar() == '=')
                {
                    getChar();
                    tok.kind = TokenKind::DotDotEqual;
                    tok.text = "..=";
                }
                else
                {
                    tok.kind = TokenKind::DotDot;
                    tok.text = "..";
                }
            }
            else
            {
                tok.kind = TokenKind::Dot;
                tok.text = ".";
            }
            break;

        case ':':
            tok.kind = TokenKind::Colon;
            tok.text = ":";
            getChar();
            break;

        case ';':
            tok.kind = TokenKind::Semicolon;
            tok.text = ";";
            getChar();
            break;

        case ',':
            tok.kind = TokenKind::Comma;
            tok.text = ",";
            getChar();
            break;

        case '@':
            tok.kind = TokenKind::At;
            tok.text = "@";
            getChar();
            break;

        case '(':
            tok.kind = TokenKind::LParen;
            tok.text = "(";
            getChar();
            break;

        case ')':
            tok.kind = TokenKind::RParen;
            tok.text = ")";
            getChar();
            break;

        case '[':
            tok.kind = TokenKind::LBracket;
            tok.text = "[";
            getChar();
            break;

        case ']':
            tok.kind = TokenKind::RBracket;
            tok.text = "]";
            getChar();
            break;

        case '{':
            tok.kind = TokenKind::LBrace;
            tok.text = "{";
            getChar();
            // Track brace depth in interpolation mode
            if (interpolationDepth_ > 0 && !braceDepth_.empty())
            {
                braceDepth_.back()++;
            }
            break;

        case '}':
            getChar();
            // Check if this closes an interpolation
            if (interpolationDepth_ > 0 && !braceDepth_.empty())
            {
                if (braceDepth_.back() == 0)
                {
                    // This closes the interpolation - continue lexing the string
                    braceDepth_.pop_back();
                    interpolationDepth_--;
                    return lexInterpolatedStringContinuation();
                }
                else
                {
                    // Just a nested brace
                    braceDepth_.back()--;
                }
            }
            tok.kind = TokenKind::RBrace;
            tok.text = "}";
            break;

        default:
            reportError(tok.loc, std::string("unexpected character '") + c + "'");
            tok.kind = TokenKind::Error;
            tok.text = std::string(1, c);
            getChar();
            break;
    }

    return tok;
}

const Token &Lexer::peek()
{
    if (!peeked_.has_value())
    {
        peeked_ = next();
    }
    return *peeked_;
}

} // namespace il::frontends::zia
