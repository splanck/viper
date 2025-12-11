//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lexer.cpp
// Purpose: Implements the Pascal lexer for tokenizing Viper Pascal source.
// Key invariants: Case-insensitive keywords; proper line/column tracking.
// Ownership/Lifetime: Lexer owns copy of source; DiagnosticEngine borrowed.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Lexer.hpp"
#include "frontends/common/CharUtils.hpp"
#include "frontends/common/NumberParsing.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string_view>

namespace il::frontends::pascal
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
        case TokenKind::RealLiteral:
            return "real";
        case TokenKind::StringLiteral:
            return "string";
        case TokenKind::Identifier:
            return "identifier";
        case TokenKind::KwAnd:
            return "and";
        case TokenKind::KwArray:
            return "array";
        case TokenKind::KwBegin:
            return "begin";
        case TokenKind::KwBreak:
            return "break";
        case TokenKind::KwCase:
            return "case";
        case TokenKind::KwClass:
            return "class";
        case TokenKind::KwConst:
            return "const";
        case TokenKind::KwConstructor:
            return "constructor";
        case TokenKind::KwContinue:
            return "continue";
        case TokenKind::KwDestructor:
            return "destructor";
        case TokenKind::KwDiv:
            return "div";
        case TokenKind::KwDo:
            return "do";
        case TokenKind::KwDownto:
            return "downto";
        case TokenKind::KwElse:
            return "else";
        case TokenKind::KwEnd:
            return "end";
        case TokenKind::KwExit:
            return "exit";
        case TokenKind::KwExcept:
            return "except";
        case TokenKind::KwFinally:
            return "finally";
        case TokenKind::KwFor:
            return "for";
        case TokenKind::KwFunction:
            return "function";
        case TokenKind::KwIf:
            return "if";
        case TokenKind::KwImplementation:
            return "implementation";
        case TokenKind::KwIn:
            return "in";
        case TokenKind::KwIs:
            return "is";
        case TokenKind::KwAs:
            return "as";
        case TokenKind::KwInherited:
            return "inherited";
        case TokenKind::KwAbstract:
            return "abstract";
        case TokenKind::KwInterface:
            return "interface";
        case TokenKind::KwMod:
            return "mod";
        case TokenKind::KwNil:
            return "nil";
        case TokenKind::KwNot:
            return "not";
        case TokenKind::KwOf:
            return "of";
        case TokenKind::KwOn:
            return "on";
        case TokenKind::KwOr:
            return "or";
        case TokenKind::KwOverride:
            return "override";
        case TokenKind::KwPrivate:
            return "private";
        case TokenKind::KwProcedure:
            return "procedure";
        case TokenKind::KwProgram:
            return "program";
        case TokenKind::KwPublic:
            return "public";
        case TokenKind::KwRaise:
            return "raise";
        case TokenKind::KwRecord:
            return "record";
        case TokenKind::KwRepeat:
            return "repeat";
        case TokenKind::KwThen:
            return "then";
        case TokenKind::KwTo:
            return "to";
        case TokenKind::KwTry:
            return "try";
        case TokenKind::KwType:
            return "type";
        case TokenKind::KwUnit:
            return "unit";
        case TokenKind::KwUntil:
            return "until";
        case TokenKind::KwUses:
            return "uses";
        case TokenKind::KwVar:
            return "var";
        case TokenKind::KwVirtual:
            return "virtual";
        case TokenKind::KwWeak:
            return "weak";
        case TokenKind::KwWhile:
            return "while";
        case TokenKind::KwWith:
            return "with";
        case TokenKind::KwSet:
            return "set";
        case TokenKind::KwForward:
            return "forward";
        case TokenKind::KwInitialization:
            return "initialization";
        case TokenKind::KwFinalization:
            return "finalization";
        case TokenKind::KwProperty:
            return "property";
        case TokenKind::Plus:
            return "+";
        case TokenKind::Minus:
            return "-";
        case TokenKind::Star:
            return "*";
        case TokenKind::Slash:
            return "/";
        case TokenKind::Equal:
            return "=";
        case TokenKind::NotEqual:
            return "<>";
        case TokenKind::Less:
            return "<";
        case TokenKind::Greater:
            return ">";
        case TokenKind::LessEqual:
            return "<=";
        case TokenKind::GreaterEqual:
            return ">=";
        case TokenKind::Assign:
            return ":=";
        case TokenKind::NilCoalesce:
            return "??";
        case TokenKind::Question:
            return "?";
        case TokenKind::Dot:
            return ".";
        case TokenKind::Comma:
            return ",";
        case TokenKind::Semicolon:
            return ";";
        case TokenKind::Colon:
            return ":";
        case TokenKind::LParen:
            return "(";
        case TokenKind::RParen:
            return ")";
        case TokenKind::LBracket:
            return "[";
        case TokenKind::RBracket:
            return "]";
        case TokenKind::Caret:
            return "^";
        case TokenKind::At:
            return "@";
        case TokenKind::DotDot:
            return "..";
    }
    return "?";
}

//===----------------------------------------------------------------------===//
// Keyword and predefined identifier tables
//===----------------------------------------------------------------------===//

namespace
{

struct KeywordEntry
{
    std::string_view key;
    TokenKind kind;
};

constexpr std::array<KeywordEntry, 59> kKeywordTable = {{
    {"abstract", TokenKind::KwAbstract},
    {"and", TokenKind::KwAnd},
    {"array", TokenKind::KwArray},
    {"as", TokenKind::KwAs},
    {"begin", TokenKind::KwBegin},
    {"break", TokenKind::KwBreak},
    {"case", TokenKind::KwCase},
    {"class", TokenKind::KwClass},
    {"const", TokenKind::KwConst},
    {"constructor", TokenKind::KwConstructor},
    {"continue", TokenKind::KwContinue},
    {"destructor", TokenKind::KwDestructor},
    {"div", TokenKind::KwDiv},
    {"do", TokenKind::KwDo},
    {"downto", TokenKind::KwDownto},
    {"else", TokenKind::KwElse},
    {"end", TokenKind::KwEnd},
    {"except", TokenKind::KwExcept},
    {"exit", TokenKind::KwExit},
    {"finalization", TokenKind::KwFinalization},
    {"finally", TokenKind::KwFinally},
    {"for", TokenKind::KwFor},
    {"forward", TokenKind::KwForward},
    {"function", TokenKind::KwFunction},
    {"if", TokenKind::KwIf},
    {"implementation", TokenKind::KwImplementation},
    {"in", TokenKind::KwIn},
    {"inherited", TokenKind::KwInherited},
    {"initialization", TokenKind::KwInitialization},
    {"interface", TokenKind::KwInterface},
    {"is", TokenKind::KwIs},
    {"mod", TokenKind::KwMod},
    {"nil", TokenKind::KwNil},
    {"not", TokenKind::KwNot},
    {"of", TokenKind::KwOf},
    {"on", TokenKind::KwOn},
    {"or", TokenKind::KwOr},
    {"override", TokenKind::KwOverride},
    {"private", TokenKind::KwPrivate},
    {"procedure", TokenKind::KwProcedure},
    {"program", TokenKind::KwProgram},
    {"property", TokenKind::KwProperty},
    {"public", TokenKind::KwPublic},
    {"raise", TokenKind::KwRaise},
    {"record", TokenKind::KwRecord},
    {"repeat", TokenKind::KwRepeat},
    {"set", TokenKind::KwSet},
    {"then", TokenKind::KwThen},
    {"to", TokenKind::KwTo},
    {"try", TokenKind::KwTry},
    {"type", TokenKind::KwType},
    {"unit", TokenKind::KwUnit},
    {"until", TokenKind::KwUntil},
    {"uses", TokenKind::KwUses},
    {"var", TokenKind::KwVar},
    {"virtual", TokenKind::KwVirtual},
    {"weak", TokenKind::KwWeak},
    {"while", TokenKind::KwWhile},
    {"with", TokenKind::KwWith},
}};

constexpr std::array<std::string_view, 9> kPredefinedTable = {
    "boolean", "exception", "false", "integer", "real", "result", "self", "string", "true"};

/// @brief Keyword lookup table (lowercase keys).
std::optional<TokenKind> lookupKeyword(std::string_view canonical)
{
    auto it = std::lower_bound(kKeywordTable.begin(),
                               kKeywordTable.end(),
                               canonical,
                               [](const KeywordEntry &entry, std::string_view key)
                               { return entry.key < key; });
    if (it != kKeywordTable.end() && it->key == canonical)
        return it->kind;
    return std::nullopt;
}

/// @brief Predefined identifiers (lowercase keys).
bool isPredefinedIdentifier(std::string_view canonical)
{
    return std::binary_search(kPredefinedTable.begin(), kPredefinedTable.end(), canonical);
}

// Use common character utilities
using ::il::frontends::common::char_utils::isDigit;
using ::il::frontends::common::char_utils::isHexDigit;
using ::il::frontends::common::char_utils::isLetter;
using ::il::frontends::common::char_utils::isWhitespace;
using ::il::frontends::common::char_utils::toLower;
using ::il::frontends::common::char_utils::toLowercase;

/// @brief Check if character can start an identifier.
/// @note Pascal identifiers start with a letter only (no underscore).
inline bool isIdentifierStart(char c)
{
    return isLetter(c);
}

/// @brief Check if character can continue an identifier.
inline bool isIdentifierContinue(char c)
{
    return isLetter(c) || isDigit(c) || c == '_';
}

} // anonymous namespace

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
    {
        return '\0';
    }
    return source_[pos_];
}

char Lexer::peekChar(size_t offset) const
{
    if (pos_ + offset >= source_.size())
    {
        return '\0';
    }
    return source_[pos_ + offset];
}

char Lexer::getChar()
{
    if (pos_ >= source_.size())
    {
        return '\0';
    }
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
        "P1000" // Pascal lexer error code
    });
}

void Lexer::skipWhitespace()
{
    while (!eof() && isWhitespace(peekChar()))
    {
        getChar();
    }
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

bool Lexer::skipBlockComment(char startChar)
{
    il::support::SourceLoc startLoc = currentLoc();

    if (startChar == '{')
    {
        // { ... } style comment
        getChar(); // consume '{'
        while (!eof())
        {
            char c = getChar();
            if (c == '}')
            {
                return true;
            }
        }
        reportError(startLoc, "unterminated block comment");
        return false;
    }
    else
    {
        // (* ... *) style comment
        getChar(); // consume '('
        getChar(); // consume '*'
        while (!eof())
        {
            char c = getChar();
            if (c == '*' && peekChar() == ')')
            {
                getChar(); // consume ')'
                return true;
            }
        }
        reportError(startLoc, "unterminated block comment");
        return false;
    }
}

void Lexer::skipWhitespaceAndComments()
{
    while (!eof())
    {
        char c = peekChar();

        // Whitespace
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

        // Block comment: { ... }
        if (c == '{')
        {
            skipBlockComment('{');
            continue;
        }

        // Block comment: (* ... *)
        if (c == '(' && peekChar(1) == '*')
        {
            skipBlockComment('(');
            continue;
        }

        // Not whitespace or comment
        break;
    }
}

std::optional<TokenKind> Lexer::lookupKeyword(const std::string &canonical)
{
    return ::il::frontends::pascal::lookupKeyword(canonical);
}

bool Lexer::isPredefinedIdentifier(const std::string &canonical)
{
    return ::il::frontends::pascal::isPredefinedIdentifier(canonical);
}

Token Lexer::lexIdentifierOrKeyword()
{
    Token tok;
    tok.loc = currentLoc();
    tok.text.reserve(16);
    tok.canonical.reserve(16);

    // Consume identifier characters
    while (!eof() && isIdentifierContinue(peekChar()))
    {
        char c = getChar();
        tok.text.push_back(c);
        tok.canonical.push_back(toLower(c));
    }

    // Check if it's a keyword
    if (auto kw = lookupKeyword(tok.canonical))
    {
        tok.kind = *kw;
        return tok;
    }

    // Check if it's a predefined identifier
    tok.kind = TokenKind::Identifier;
    tok.isPredefined = isPredefinedIdentifier(tok.canonical);
    return tok;
}

Token Lexer::lexNumber()
{
    Token tok;
    tok.loc = currentLoc();
    tok.kind = TokenKind::IntegerLiteral;

    // Consume integer part
    while (!eof() && isDigit(peekChar()))
    {
        tok.text.push_back(getChar());
    }

    // Check for decimal point (but not .. range operator)
    if (peekChar() == '.' && peekChar(1) != '.')
    {
        tok.kind = TokenKind::RealLiteral;
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
        tok.kind = TokenKind::RealLiteral;
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

    // Parse the value using common number parsing utilities
    tok.canonical = tok.text;
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
        tok.kind = TokenKind::RealLiteral;
        tok.realValue = parsed.floatValue;
    }
    else
    {
        tok.kind = TokenKind::IntegerLiteral;
        tok.intValue = parsed.intValue;
    }

    return tok;
}

Token Lexer::lexHexNumber()
{
    Token tok;
    tok.loc = currentLoc();
    tok.kind = TokenKind::IntegerLiteral;

    tok.text.push_back(getChar()); // consume '$'

    // Must have at least one hex digit
    if (!isHexDigit(peekChar()))
    {
        reportError(tok.loc, "invalid hex literal: expected hex digits after $");
        tok.kind = TokenKind::Error;
        return tok;
    }

    // Consume hex digits
    while (!eof() && isHexDigit(peekChar()))
    {
        tok.text.push_back(getChar());
    }

    // Parse the hex value using common utilities (skip the '$' prefix)
    tok.canonical = tok.text;
    std::string_view hexDigits(tok.text.data() + 1, tok.text.size() - 1);
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

Token Lexer::lexString()
{
    Token tok;
    tok.loc = currentLoc();
    tok.kind = TokenKind::StringLiteral;

    tok.text.push_back(getChar()); // consume opening quote

    std::string value; // the actual string content (without quotes, with escapes processed)

    while (!eof())
    {
        char c = peekChar();

        // Check for newline inside string (error)
        if (c == '\n' || c == '\r')
        {
            reportError(tok.loc, "newline in string literal");
            tok.kind = TokenKind::Error;
            return tok;
        }

        // Check for closing quote
        if (c == '\'')
        {
            tok.text.push_back(getChar()); // consume quote

            // Check for doubled apostrophe (escaped single quote)
            if (peekChar() == '\'')
            {
                tok.text.push_back(getChar()); // consume second quote
                value.push_back('\'');         // add single quote to value
                continue;
            }

            // End of string
            tok.canonical = value;
            return tok;
        }

        // Regular character
        tok.text.push_back(getChar());
        value.push_back(c);
    }

    // Reached EOF without closing quote
    reportError(tok.loc, "unterminated string literal");
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

    // Hex number
    if (c == '$')
    {
        return lexHexNumber();
    }

    // String literal
    if (c == '\'')
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
            tok.kind = TokenKind::Minus;
            tok.text = "-";
            getChar();
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

        case '=':
            tok.kind = TokenKind::Equal;
            tok.text = "=";
            getChar();
            break;

        case '<':
            getChar();
            if (peekChar() == '>')
            {
                getChar();
                tok.kind = TokenKind::NotEqual;
                tok.text = "<>";
            }
            else if (peekChar() == '=')
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

        case ':':
            getChar();
            if (peekChar() == '=')
            {
                getChar();
                tok.kind = TokenKind::Assign;
                tok.text = ":=";
            }
            else
            {
                tok.kind = TokenKind::Colon;
                tok.text = ":";
            }
            break;

        case '?':
            getChar();
            if (peekChar() == '?')
            {
                getChar();
                tok.kind = TokenKind::NilCoalesce;
                tok.text = "??";
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
                tok.kind = TokenKind::DotDot;
                tok.text = "..";
            }
            else
            {
                tok.kind = TokenKind::Dot;
                tok.text = ".";
            }
            break;

        case ',':
            tok.kind = TokenKind::Comma;
            tok.text = ",";
            getChar();
            break;

        case ';':
            tok.kind = TokenKind::Semicolon;
            tok.text = ";";
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

        case '^':
            tok.kind = TokenKind::Caret;
            tok.text = "^";
            getChar();
            break;

        case '@':
            tok.kind = TokenKind::At;
            tok.text = "@";
            getChar();
            break;

        default:
            reportError(tok.loc, std::string("unexpected character '") + c + "'");
            tok.kind = TokenKind::Error;
            tok.text = std::string(1, c);
            getChar();
            break;
    }

    tok.canonical = tok.text;
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

} // namespace il::frontends::pascal
