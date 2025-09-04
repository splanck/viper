// File: src/frontends/basic/Lexer.cpp
// Purpose: Implements lexer for the BASIC frontend.
// Key invariants: None.
// Ownership/Lifetime: Lexer views input managed externally.
// Links: docs/class-catalog.md

#include "frontends/basic/Lexer.hpp"
#include <cctype>
#include <string>

namespace il::frontends::basic
{

Lexer::Lexer(std::string_view src, uint32_t file_id) : src_(src), file_id_(file_id) {}

char Lexer::peek() const
{
    return pos_ < src_.size() ? src_[pos_] : '\0';
}

char Lexer::get()
{
    if (pos_ >= src_.size())
        return '\0';
    char c = src_[pos_++];
    if (c == '\n')
    {
        line_++;
        column_ = 1;
    }
    else
    {
        column_++;
    }
    return c;
}

bool Lexer::eof() const
{
    return pos_ >= src_.size();
}

void Lexer::skipWhitespaceExceptNewline()
{
    while (!eof())
    {
        char c = peek();
        if (c != '\n' && std::isspace(static_cast<unsigned char>(c)))
        {
            get();
        }
        else
        {
            break;
        }
    }
}

Token Lexer::lexNumber()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string s;
    bool seenDot = false;
    bool seenExp = false;
    bool seenHash = false;
    if (peek() == '.')
    {
        seenDot = true;
        s.push_back(get());
    }
    while (std::isdigit(peek()))
        s.push_back(get());
    if (!seenDot && peek() == '.')
    {
        seenDot = true;
        s.push_back(get());
        while (std::isdigit(peek()))
            s.push_back(get());
    }
    if ((peek() == 'e' || peek() == 'E'))
    {
        seenExp = true;
        s.push_back(get());
        if (peek() == '+' || peek() == '-')
            s.push_back(get());
        while (std::isdigit(peek()))
            s.push_back(get());
    }
    if (peek() == '#')
    {
        seenHash = true;
        get();
    }
    (void)seenDot;
    (void)seenExp;
    if (seenHash)
        s.push_back('#');
    return {TokenKind::Number, s, loc};
}

Token Lexer::lexIdentifierOrKeyword()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string s;
    while (std::isalnum(peek()) || peek() == '$' || peek() == '#')
        s.push_back(std::toupper(get()));
    // keywords
    if (s == "PRINT")
        return {TokenKind::KeywordPrint, s, loc};
    if (s == "LET")
        return {TokenKind::KeywordLet, s, loc};
    if (s == "IF")
        return {TokenKind::KeywordIf, s, loc};
    if (s == "THEN")
        return {TokenKind::KeywordThen, s, loc};
    if (s == "ELSE")
        return {TokenKind::KeywordElse, s, loc};
    if (s == "ELSEIF")
        return {TokenKind::KeywordElseIf, s, loc};
    if (s == "WHILE")
        return {TokenKind::KeywordWhile, s, loc};
    if (s == "WEND")
        return {TokenKind::KeywordWend, s, loc};
    if (s == "FOR")
        return {TokenKind::KeywordFor, s, loc};
    if (s == "TO")
        return {TokenKind::KeywordTo, s, loc};
    if (s == "STEP")
        return {TokenKind::KeywordStep, s, loc};
    if (s == "NEXT")
        return {TokenKind::KeywordNext, s, loc};
    if (s == "GOTO")
        return {TokenKind::KeywordGoto, s, loc};
    if (s == "END")
        return {TokenKind::KeywordEnd, s, loc};
    if (s == "INPUT")
        return {TokenKind::KeywordInput, s, loc};
    if (s == "DIM")
        return {TokenKind::KeywordDim, s, loc};
    if (s == "RANDOMIZE")
        return {TokenKind::KeywordRandomize, s, loc};
    if (s == "AND")
        return {TokenKind::KeywordAnd, s, loc};
    if (s == "OR")
        return {TokenKind::KeywordOr, s, loc};
    if (s == "NOT")
        return {TokenKind::KeywordNot, s, loc};
    if (s == "MOD")
        return {TokenKind::KeywordMod, s, loc};
    if (s == "SQR")
        return {TokenKind::KeywordSqr, s, loc};
    if (s == "ABS")
        return {TokenKind::KeywordAbs, s, loc};
    if (s == "FLOOR")
        return {TokenKind::KeywordFloor, s, loc};
    if (s == "CEIL")
        return {TokenKind::KeywordCeil, s, loc};
    if (s == "SIN")
        return {TokenKind::KeywordSin, s, loc};
    if (s == "COS")
        return {TokenKind::KeywordCos, s, loc};
    if (s == "POW")
        return {TokenKind::KeywordPow, s, loc};
    if (s == "RND")
        return {TokenKind::KeywordRnd, s, loc};
    return {TokenKind::Identifier, s, loc};
}

Token Lexer::lexString()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string s;
    get(); // consume opening quote
    while (!eof() && peek() != '"')
        s.push_back(get());
    if (peek() == '"')
        get();
    return {TokenKind::String, s, loc};
}

Token Lexer::next()
{
    skipWhitespaceExceptNewline();
    il::support::SourceLoc loc{file_id_, line_, column_};
    if (eof())
        return {TokenKind::EndOfFile, "", loc};
    char c = peek();
    if (c == '\n')
    {
        get();
        return {TokenKind::EndOfLine, "\n", loc};
    }
    if (std::isdigit(c) || (c == '.' && pos_ + 1 < src_.size() && std::isdigit(src_[pos_ + 1])))
        return lexNumber();
    if (std::isalpha(c))
        return lexIdentifierOrKeyword();
    if (c == '"')
        return lexString();
    get();
    switch (c)
    {
        case '+':
            return {TokenKind::Plus, "+", loc};
        case '-':
            return {TokenKind::Minus, "-", loc};
        case '*':
            return {TokenKind::Star, "*", loc};
        case '/':
            return {TokenKind::Slash, "/", loc};
        case '\\':
            return {TokenKind::Backslash, "\\", loc};
        case '=':
            return {TokenKind::Equal, "=", loc};
        case '<':
            if (peek() == '>')
            {
                get();
                return {TokenKind::NotEqual, "<>", loc};
            }
            if (peek() == '=')
            {
                get();
                return {TokenKind::LessEqual, "<=", loc};
            }
            return {TokenKind::Less, "<", loc};
        case '>':
            if (peek() == '=')
            {
                get();
                return {TokenKind::GreaterEqual, ">=", loc};
            }
            return {TokenKind::Greater, ">", loc};
        case '(':
            return {TokenKind::LParen, "(", loc};
        case ')':
            return {TokenKind::RParen, ")", loc};
        case ',':
            return {TokenKind::Comma, ",", loc};
        case ';':
            return {TokenKind::Semicolon, ";", loc};
        case ':':
            return {TokenKind::Colon, ":", loc};
    }
    return {TokenKind::EndOfFile, "", loc};
}

} // namespace il::frontends::basic
