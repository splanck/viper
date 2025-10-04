// File: src/frontends/basic/Lexer.cpp
// Purpose: Implements lexical analysis for BASIC source with line-aware scanning
//          and comment skipping.
// Key invariants: pos_ indexes into src_; line_ and column_ reflect the current
//                 character position.
// Ownership/Lifetime: Lexer borrows the source buffer; caller retains
//                     ownership.
// Links: docs/codemap.md

#include "frontends/basic/Lexer.hpp"
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace il::frontends::basic
{

namespace
{
struct KeywordEntry
{
    std::string_view lexeme;
    TokenKind kind;
};

constexpr std::array<KeywordEntry, 59> kKeywordTable{{
    {"ABS", TokenKind::KeywordAbs},
    {"AND", TokenKind::KeywordAnd},
    {"ANDALSO", TokenKind::KeywordAndAlso},
    {"APPEND", TokenKind::KeywordAppend},
    {"AS", TokenKind::KeywordAs},
    {"BINARY", TokenKind::KeywordBinary},
    {"BOOLEAN", TokenKind::KeywordBoolean},
    {"CEIL", TokenKind::KeywordCeil},
    {"CLOSE", TokenKind::KeywordClose},
    {"CLS", TokenKind::KeywordCls},
    {"COLOR", TokenKind::KeywordColor},
    {"COS", TokenKind::KeywordCos},
    {"DIM", TokenKind::KeywordDim},
    {"DO", TokenKind::KeywordDo},
    {"ELSE", TokenKind::KeywordElse},
    {"ELSEIF", TokenKind::KeywordElseIf},
    {"END", TokenKind::KeywordEnd},
    {"EOF", TokenKind::KeywordEof},
    {"ERROR", TokenKind::KeywordError},
    {"EXIT", TokenKind::KeywordExit},
    {"FALSE", TokenKind::KeywordFalse},
    {"FLOOR", TokenKind::KeywordFloor},
    {"FOR", TokenKind::KeywordFor},
    {"FUNCTION", TokenKind::KeywordFunction},
    {"GOTO", TokenKind::KeywordGoto},
    {"IF", TokenKind::KeywordIf},
    {"INPUT", TokenKind::KeywordInput},
    {"LBOUND", TokenKind::KeywordLbound},
    {"LET", TokenKind::KeywordLet},
    {"LINE", TokenKind::KeywordLine},
    {"LOCATE", TokenKind::KeywordLocate},
    {"LOOP", TokenKind::KeywordLoop},
    {"MOD", TokenKind::KeywordMod},
    {"NEXT", TokenKind::KeywordNext},
    {"NOT", TokenKind::KeywordNot},
    {"ON", TokenKind::KeywordOn},
    {"OPEN", TokenKind::KeywordOpen},
    {"OR", TokenKind::KeywordOr},
    {"ORELSE", TokenKind::KeywordOrElse},
    {"OUTPUT", TokenKind::KeywordOutput},
    {"POW", TokenKind::KeywordPow},
    {"PRINT", TokenKind::KeywordPrint},
    {"RANDOM", TokenKind::KeywordRandom},
    {"RANDOMIZE", TokenKind::KeywordRandomize},
    {"REDIM", TokenKind::KeywordRedim},
    {"RESUME", TokenKind::KeywordResume},
    {"RETURN", TokenKind::KeywordReturn},
    {"RND", TokenKind::KeywordRnd},
    {"SIN", TokenKind::KeywordSin},
    {"SQR", TokenKind::KeywordSqr},
    {"STEP", TokenKind::KeywordStep},
    {"SUB", TokenKind::KeywordSub},
    {"THEN", TokenKind::KeywordThen},
    {"TO", TokenKind::KeywordTo},
    {"TRUE", TokenKind::KeywordTrue},
    {"UBOUND", TokenKind::KeywordUbound},
    {"UNTIL", TokenKind::KeywordUntil},
    {"WEND", TokenKind::KeywordWend},
    {"WHILE", TokenKind::KeywordWhile},
}};

constexpr bool isKeywordTableSorted()
{
    for (std::size_t i = 1; i < kKeywordTable.size(); ++i)
    {
        if (!(kKeywordTable[i - 1].lexeme < kKeywordTable[i].lexeme))
            return false;
    }
    return true;
}

static_assert(isKeywordTableSorted(), "Keyword table must be sorted lexicographically");

TokenKind lookupKeyword(std::string_view lexeme)
{
    auto first = kKeywordTable.begin();
    auto last = kKeywordTable.end();
    while (first < last)
    {
        auto mid = first + (last - first) / 2;
        if (mid->lexeme == lexeme)
            return mid->kind;
        if (mid->lexeme < lexeme)
        {
            first = mid + 1;
        }
        else
        {
            last = mid;
        }
    }
    return TokenKind::Identifier;
}

} // namespace

/// @brief Construct a lexer over the given source buffer.
/// @param src BASIC program text to scan; must remain valid for the lexer
///             lifetime.
/// @param file_id Identifier used when emitting diagnostic locations.
/// @details Initializes position tracking to the beginning of @p src.
Lexer::Lexer(std::string_view src, uint32_t file_id) : src_(src), file_id_(file_id) {}

/// @brief Peek at the current character without consuming it.
/// @return The current character, or '\0' if the lexer is at end of input.
/// @note Does not modify the lexer's state.
char Lexer::peek() const
{
    return pos_ < src_.size() ? src_[pos_] : '\0';
}

/// @brief Consume and return the next character from the source.
/// @return The consumed character, or '\0' if already at end of input.
/// @details Advances internal position; @p line_ and @p column_ are updated,
///         resetting column to 1 after a newline.
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

/// @brief Determine whether all input has been consumed.
/// @return True if @p pos_ is at or beyond the end of the buffer.
/// @note Has no side effects.
bool Lexer::eof() const
{
    return pos_ >= src_.size();
}

/// @brief Skip spaces, tabs, and carriage returns but stop at newlines.
/// @details Advances @p pos_, @p line_, and @p column_ for each consumed
///         character while leaving newlines untouched for tokenization.
void Lexer::skipWhitespaceExceptNewline()
{
    while (!eof())
    {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r')
        {
            get();
        }
        else
        {
            break;
        }
    }
}

/// @brief Skip whitespace and BASIC comments starting with <tt>'</tt> or REM.
/// @details Consumes characters until a newline or non-comment character is
///         encountered.
/// @note Preserves the terminating newline for the caller.
void Lexer::skipWhitespaceAndComments()
{
    while (true)
    {
        skipWhitespaceExceptNewline();

        if (peek() == '\'')
        {
            while (!eof() && peek() != '\n')
                get();
            continue;
        }

        if (std::toupper(static_cast<unsigned char>(peek())) == 'R' && pos_ + 2 < src_.size() &&
            std::toupper(static_cast<unsigned char>(src_[pos_ + 1])) == 'E' &&
            std::toupper(static_cast<unsigned char>(src_[pos_ + 2])) == 'M')
        {
            char after = (pos_ + 3 < src_.size()) ? src_[pos_ + 3] : '\0';
            if (!std::isalnum(static_cast<unsigned char>(after)) && after != '$' && after != '#')
            {
                get();
                get();
                get();
                while (!eof() && peek() != '\n')
                    get();
                continue;
            }
        }

        break;
    }
}

/// @brief Lex a numeric literal including optional fraction, exponent, and type
///        suffix <tt>#</tt>.
/// @return Token of kind Number representing the characters consumed.
/// @details Advances @p pos_, @p line_, and @p column_ while collecting
///         characters. Numeric format is minimally validated.
Token Lexer::lexNumber()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string s;
    bool seenDot = false;
    bool seenExp = false;
    bool seenHash = false;
    bool seenBang = false;
    if (peek() == '.')
    {
        seenDot = true;
        s.push_back(get());
    }
    while (std::isdigit(static_cast<unsigned char>(peek())))
        s.push_back(get());
    if (!seenDot && peek() == '.')
    {
        seenDot = true;
        s.push_back(get());
        while (std::isdigit(static_cast<unsigned char>(peek())))
            s.push_back(get());
    }
    if ((peek() == 'e' || peek() == 'E'))
    {
        seenExp = true;
        s.push_back(get());
        if (peek() == '+' || peek() == '-')
            s.push_back(get());
        while (std::isdigit(static_cast<unsigned char>(peek())))
            s.push_back(get());
    }
    if (peek() == '#' || peek() == '!')
    {
        char suffix = get();
        if (suffix == '#')
            seenHash = true;
        else if (suffix == '!')
            seenBang = true;
    }
    (void)seenDot;
    (void)seenExp;
    if (seenHash)
        s.push_back('#');
    if (seenBang)
        s.push_back('!');
    return {TokenKind::Number, s, loc};
}

/// @brief Lex an identifier or reserved keyword.
/// @return Identifier or keyword token; identifiers are uppercased for
///         keyword comparison.
/// @details Consumes alphanumeric characters plus optional trailing '$' or '#'
///         and advances position counters accordingly.
Token Lexer::lexIdentifierOrKeyword()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string s;
    while (std::isalnum(static_cast<unsigned char>(peek())))
        s.push_back(std::toupper(static_cast<unsigned char>(get())));
    if (peek() == '$' || peek() == '#' || peek() == '!')
        s.push_back(std::toupper(static_cast<unsigned char>(get())));
    TokenKind kind = lookupKeyword(s);
    return {kind, s, loc};
}

/// @brief Lex a string literal delimited by double quotes.
/// @return String token containing characters between quotes.
/// @details Consumes the opening quote and then reads characters until a
///         closing quote or end of file; no escape sequences are processed.
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

/// @brief Retrieve the next token from the input stream.
/// @return The next token, which may be EndOfLine or EndOfFile.
/// @details Skips whitespace and comments, updating line and column counters as
///         characters are consumed. Newlines yield an EndOfLine token so
///         higher-level parsers can maintain line structure.
Token Lexer::next()
{
    // Skip leading spaces and tabs but preserve newlines for tokenization.
    skipWhitespaceAndComments();

    if (eof())
        return {TokenKind::EndOfFile, "", {file_id_, line_, column_}};

    char c = peek();

    // Handle newline explicitly so skipWhitespaceAndComments is called only once.
    if (c == '\n')
    {
        il::support::SourceLoc loc{file_id_, line_, column_};
        get();
        return {TokenKind::EndOfLine, "\n", loc};
    }

    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && pos_ + 1 < src_.size() &&
         std::isdigit(static_cast<unsigned char>(src_[pos_ + 1]))))
        return lexNumber();
    if (std::isalpha(static_cast<unsigned char>(c)))
        return lexIdentifierOrKeyword();
    if (c == '"')
        return lexString();

    il::support::SourceLoc loc{file_id_, line_, column_};
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
        case '^':
            return {TokenKind::Caret, "^", loc};
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
        case '#':
            return {TokenKind::Hash, "#", loc};
    }
    return {TokenKind::Unknown, std::string(1, c), loc};
}

} // namespace il::frontends::basic
