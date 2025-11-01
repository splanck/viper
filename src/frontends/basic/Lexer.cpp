//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lexer.cpp
// Purpose: Tokenise BASIC source text into the stream consumed by the parser
//          while preserving line-structured layout for diagnostic reporting.
// Key invariants: The cursor state (`pos_`, `line_`, and `column_`) always
//                 reflects the next character to be read; keyword lookups use a
//                 binary search over a sorted static table.
// Ownership/Lifetime: The lexer borrows the source buffer for the duration of
//                     scanning and never allocates persistent state beyond
//                     temporary token strings.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

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

constexpr std::array<KeywordEntry, 72> kKeywordTable{{
    {"ABS", TokenKind::KeywordAbs},
    {"AND", TokenKind::KeywordAnd},
    {"ANDALSO", TokenKind::KeywordAndAlso},
    {"APPEND", TokenKind::KeywordAppend},
    {"AS", TokenKind::KeywordAs},
    {"BINARY", TokenKind::KeywordBinary},
    {"BOOLEAN", TokenKind::KeywordBoolean},
    {"CASE", TokenKind::KeywordCase},
    {"CEIL", TokenKind::KeywordCeil},
    {"CLASS", TokenKind::KeywordClass},
    {"CLOSE", TokenKind::KeywordClose},
    {"CLS", TokenKind::KeywordCls},
    {"COLOR", TokenKind::KeywordColor},
    {"COS", TokenKind::KeywordCos},
    {"DELETE", TokenKind::KeywordDelete},
    {"DESTRUCTOR", TokenKind::KeywordDestructor},
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
    {"GOSUB", TokenKind::KeywordGosub},
    {"GOTO", TokenKind::KeywordGoto},
    {"IF", TokenKind::KeywordIf},
    {"INPUT", TokenKind::KeywordInput},
    {"LBOUND", TokenKind::KeywordLbound},
    {"LET", TokenKind::KeywordLet},
    {"LINE", TokenKind::KeywordLine},
    {"LOC", TokenKind::KeywordLoc},
    {"LOCATE", TokenKind::KeywordLocate},
    {"LOF", TokenKind::KeywordLof},
    {"LOOP", TokenKind::KeywordLoop},
    {"ME", TokenKind::KeywordMe},
    {"MOD", TokenKind::KeywordMod},
    {"NEW", TokenKind::KeywordNew},
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
    {"SEEK", TokenKind::KeywordSeek},
    {"SELECT", TokenKind::KeywordSelect},
    {"SIN", TokenKind::KeywordSin},
    {"SQR", TokenKind::KeywordSqr},
    {"STEP", TokenKind::KeywordStep},
    {"SUB", TokenKind::KeywordSub},
    {"THEN", TokenKind::KeywordThen},
    {"TO", TokenKind::KeywordTo},
    {"TRUE", TokenKind::KeywordTrue},
    {"TYPE", TokenKind::KeywordType},
    {"UBOUND", TokenKind::KeywordUbound},
    {"UNTIL", TokenKind::KeywordUntil},
    {"WEND", TokenKind::KeywordWend},
    {"WHILE", TokenKind::KeywordWhile},
    {"WRITE", TokenKind::KeywordWrite},
}};

/// @brief Confirm the keyword table maintains the ordering required for binary search.
///
/// @details The lexer performs binary searches over @ref kKeywordTable.  Keeping
///          the table sorted enables the compile-time static assert below to
///          catch accidental edits that disturb the invariant.
/// @return @c true when each lexeme is lexicographically ordered before its
///         successor.
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

/// @brief Lookup a candidate identifier in the keyword table.
///
/// @details Performs a manual binary search across @ref kKeywordTable to avoid
///          introducing dynamic allocations.  The search compares the provided
///          lexeme against the pre-sorted static table and yields the
///          corresponding token kind when a match is found.
/// @param lexeme Uppercased identifier text to classify.
/// @return Keyword kind when recognised; @ref TokenKind::Identifier otherwise.
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
///
/// @details Stores lightweight views into @p src and primes the position
///          counters so the first call to @ref next observes the opening
///          character at line 1, column 1.  The caller retains ownership of the
///          underlying buffer for the lexer's lifetime.
/// @param src BASIC program text to scan; must outlive the lexer instance.
/// @param file_id Identifier used when emitting diagnostic locations.
Lexer::Lexer(std::string_view src, uint32_t file_id) : src_(src), file_id_(file_id) {}

/// @brief Peek at the current character without consuming it.
///
/// @details Returns the byte located at the current cursor position.  When the
///          cursor has advanced past the end of the buffer, the sentinel
///          character @c '\0' is returned instead so callers can test for
///          exhaustion without modifying state.
/// @return The current character, or '\0' if the lexer is at end of input.
char Lexer::peek() const
{
    return pos_ < src_.size() ? src_[pos_] : '\0';
}

/// @brief Consume and return the next character from the source.
///
/// @details Advances @ref pos_ by one and updates @ref line_/@ref column_ to
///          maintain diagnostic accuracy.  Newlines increment the line counter
///          and reset the column to one, whereas other characters simply bump
///          the column.  When invoked at end of input the function returns the
///          sentinel @c '\0' and leaves the state unchanged.
/// @return The consumed character, or '\0' if already at end of input.
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
///
/// @details This lightweight predicate powers loops that walk forward until a
///          newline or delimiter is encountered without performing bounds
///          checks on each iteration.  The method leaves the cursor untouched
///          and can therefore be called freely.
/// @return @c true if @ref pos_ is at or beyond the end of the buffer.
bool Lexer::eof() const
{
    return pos_ >= src_.size();
}

/// @brief Lex an identifier or reserved keyword.
///
/// @details Characters are uppercased while they are consumed so keyword lookup
///          becomes a straightforward table search.  Optional type suffixes are
///          folded into the token text to match the semantics of the BASIC type
///          inference rules applied later in the pipeline.
/// @return Identifier or keyword token; identifiers are uppercased for keyword
///         comparison.
Token Lexer::lexIdentifierOrKeyword()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string s;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
        s.push_back(std::toupper(static_cast<unsigned char>(get())));
    if (peek() == '$' || peek() == '#' || peek() == '!' || peek() == '%' || peek() == '&')
        s.push_back(std::toupper(static_cast<unsigned char>(get())));
    TokenKind kind = lookupKeyword(s);
    return {kind, s, loc};
}

/// @brief Retrieve the next token from the input stream.
///
/// @details Skips insignificant trivia, returns explicit newline tokens, and
///          dispatches to specialised lexers for numbers, identifiers, and
///          strings.  Punctuation is handled inline via a switch statement to
///          keep hot paths branch-friendly.  Location metadata is captured for
///          every token so diagnostics can point back to the source program.
/// @return The next token, which may be EndOfLine or EndOfFile.
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
        case '.':
        {
            // If previous and next chars are digits, this is part of a numeric literal; fallthrough
            // to number logic. Otherwise, return TokenKind::Dot.
            bool prevIsDigit = false;
            if (pos_ >= 2)
            {
                unsigned char prev = static_cast<unsigned char>(src_[pos_ - 2]);
                prevIsDigit = std::isdigit(prev) != 0;
            }
            bool nextIsDigit = false;
            if (pos_ < src_.size())
            {
                unsigned char next = static_cast<unsigned char>(src_[pos_]);
                nextIsDigit = std::isdigit(next) != 0;
            }
            if (prevIsDigit && nextIsDigit)
            {
                if (column_ > 1)
                    --column_;
                --pos_;
                return lexNumber();
            }
            return {TokenKind::Dot, ".", loc};
        }
    }
    return {TokenKind::Unknown, std::string(1, c), loc};
}

} // namespace il::frontends::basic
