//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

constexpr std::array<KeywordEntry, 97> kKeywordTable{{
    {"ABS", TokenKind::KeywordAbs},
    {"ABSTRACT", TokenKind::KeywordAbstract},
    {"ADDFILE", TokenKind::KeywordAddfile},
    {"ALTSCREEN", TokenKind::KeywordAltscreen},
    {"AND", TokenKind::KeywordAnd},
    {"ANDALSO", TokenKind::KeywordAndAlso},
    {"APPEND", TokenKind::KeywordAppend},
    {"AS", TokenKind::KeywordAs},
    {"BASE", TokenKind::KeywordBase},
    {"BEEP", TokenKind::KeywordBeep},
    {"BINARY", TokenKind::KeywordBinary},
    {"BOOLEAN", TokenKind::KeywordBoolean},
    {"CASE", TokenKind::KeywordCase},
    {"CATCH", TokenKind::KeywordCatch},
    {"CEIL", TokenKind::KeywordCeil},
    {"CLASS", TokenKind::KeywordClass},
    {"CLOSE", TokenKind::KeywordClose},
    {"CLS", TokenKind::KeywordCls},
    {"COLOR", TokenKind::KeywordColor},
    {"CONST", TokenKind::KeywordConst},
    {"COS", TokenKind::KeywordCos},
    {"CURSOR", TokenKind::KeywordCursor},
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
    {"FINAL", TokenKind::KeywordFinal},
    {"FLOOR", TokenKind::KeywordFloor},
    {"FOR", TokenKind::KeywordFor},
    {"FUNCTION", TokenKind::KeywordFunction},
    {"GOSUB", TokenKind::KeywordGosub},
    {"GOTO", TokenKind::KeywordGoto},
    {"IF", TokenKind::KeywordIf},
    {"IMPLEMENTS", TokenKind::KeywordImplements},
    {"INPUT", TokenKind::KeywordInput},
    {"INTERFACE", TokenKind::KeywordInterface},
    {"IS", TokenKind::KeywordIs},
    {"LBOUND", TokenKind::KeywordLbound},
    {"LET", TokenKind::KeywordLet},
    // NOTE: 'LINE' is treated as a soft keyword: lex as identifier; parser recognises 'LINE INPUT'.
    {"LOC", TokenKind::KeywordLoc},
    {"LOCATE", TokenKind::KeywordLocate},
    {"LOF", TokenKind::KeywordLof},
    {"LOOP", TokenKind::KeywordLoop},
    {"ME", TokenKind::KeywordMe},
    {"MOD", TokenKind::KeywordMod},
    {"NAMESPACE", TokenKind::KeywordNamespace},
    {"NEW", TokenKind::KeywordNew},
    {"NEXT", TokenKind::KeywordNext},
    {"NOT", TokenKind::KeywordNot},
    {"OFF", TokenKind::KeywordOff},
    {"ON", TokenKind::KeywordOn},
    {"OPEN", TokenKind::KeywordOpen},
    {"OR", TokenKind::KeywordOr},
    {"ORELSE", TokenKind::KeywordOrElse},
    {"OUTPUT", TokenKind::KeywordOutput},
    {"OVERRIDE", TokenKind::KeywordOverride},
    {"POW", TokenKind::KeywordPow},
    {"PRESERVE", TokenKind::KeywordPreserve},
    {"PRINT", TokenKind::KeywordPrint},
    {"PRIVATE", TokenKind::KeywordPrivate},
    {"PROPERTY", TokenKind::KeywordProperty},
    {"PUBLIC", TokenKind::KeywordPublic},
    {"RANDOM", TokenKind::KeywordRandom},
    {"RANDOMIZE", TokenKind::KeywordRandomize},
    {"REDIM", TokenKind::KeywordRedim},
    {"RESUME", TokenKind::KeywordResume},
    {"RETURN", TokenKind::KeywordReturn},
    {"RND", TokenKind::KeywordRnd},
    {"SEEK", TokenKind::KeywordSeek},
    {"SELECT", TokenKind::KeywordSelect},
    {"SHARED", TokenKind::KeywordShared},
    {"SIN", TokenKind::KeywordSin},
    {"SLEEP", TokenKind::KeywordSleep},
    {"SQR", TokenKind::KeywordSqr},
    {"STATIC", TokenKind::KeywordStatic},
    {"STEP", TokenKind::KeywordStep},
    {"SUB", TokenKind::KeywordSub},
    {"SWAP", TokenKind::KeywordSwap},
    {"THEN", TokenKind::KeywordThen},
    {"TO", TokenKind::KeywordTo},
    {"TRUE", TokenKind::KeywordTrue},
    {"TRY", TokenKind::KeywordTry},
    {"TYPE", TokenKind::KeywordType},
    {"UBOUND", TokenKind::KeywordUbound},
    {"UNTIL", TokenKind::KeywordUntil},
    {"USING", TokenKind::KeywordUsing},
    {"VIRTUAL", TokenKind::KeywordVirtual},
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
/// @brief Implements for functionality.
/// @param kKeywordTable.size( Parameter description needed.
/// @return Return value description needed.
    for (std::size_t i = 1; i < kKeywordTable.size(); ++i)
    {
/// @brief Implements if functionality.
/// @param kKeywordTable[i].lexeme Parameter description needed.
/// @return Return value description needed.
        if (!(kKeywordTable[i - 1].lexeme < kKeywordTable[i].lexeme))
            return false;
    }
    return true;
}

/// @brief Implements static_assert functionality.
/// @param isKeywordTableSorted( Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param lexeme.size( Parameter description needed.
/// @return Return value description needed.
    if (lexeme.size() == 2)
    {
        unsigned char first = static_cast<unsigned char>(lexeme[0]);
        unsigned char second = static_cast<unsigned char>(lexeme[1]);
/// @brief Implements if functionality.
/// @param std::toupper(first Parameter description needed.
/// @return Return value description needed.
        if (std::toupper(first) == 'M' && std::toupper(second) == 'E')
            lexeme = "ME";
    }
    auto first = kKeywordTable.begin();
    auto last = kKeywordTable.end();
/// @brief Implements while functionality.
/// @param last Parameter description needed.
/// @return Return value description needed.
    while (first < last)
    {
        auto mid = first + (last - first) / 2;
/// @brief Implements if functionality.
/// @param lexeme Parameter description needed.
/// @return Return value description needed.
        if (mid->lexeme == lexeme)
            return mid->kind;
/// @brief Implements if functionality.
/// @param lexeme Parameter description needed.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param src_.size( Parameter description needed.
/// @return Return value description needed.
    if (pos_ >= src_.size())
        return '\0';
    char c = src_[pos_++];
/// @brief Implements if functionality.
/// @param '\n' Parameter description needed.
/// @return Return value description needed.
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

/// @brief Skip spaces, tabs, and carriage returns but stop at newlines.
///
/// @details Whitespace between statements is ignored by BASIC except for
///          newline boundaries that influence statement grouping.  This helper
///          advances the cursor past horizontal whitespace while keeping
///          newlines in the stream for later tokenisation.
void Lexer::skipWhitespaceExceptNewline()
{
/// @brief Implements while functionality.
/// @param !eof( Parameter description needed.
/// @return Return value description needed.
    while (!eof())
    {
        char c = peek();
/// @brief Implements if functionality.
/// @param '\r' Parameter description needed.
/// @return Return value description needed.
        if (c == ' ' || c == '\t' || c == '\r')
        {
/// @brief Retrieves  value.
/// @return Return value description needed.
            get();
        }
        else
        {
            break;
        }
    }
}

/// @brief Skip whitespace and BASIC comments starting with <tt>'</tt> or REM.
///
/// @details BASIC treats apostrophe-prefixed and "REM" tokens as
///          rest-of-line comments.  The helper repeatedly removes whitespace and
///          comment bodies so the next significant token begins at the current
///          cursor.  The newline terminating a comment is preserved so callers
///          can emit @ref TokenKind::EndOfLine.
void Lexer::skipWhitespaceAndComments()
{
/// @brief Implements while functionality.
/// @param true Parameter description needed.
/// @return Return value description needed.
    while (true)
    {
/// @brief Implements skipWhitespaceExceptNewline functionality.
/// @return Return value description needed.
        skipWhitespaceExceptNewline();

/// @brief Implements if functionality.
/// @param peek( Parameter description needed.
/// @return Return value description needed.
        if (peek() == '\'')
        {
/// @brief Implements while functionality.
/// @param !eof( Parameter description needed.
/// @return Return value description needed.
            while (!eof() && peek() != '\n')
/// @brief Retrieves  value.
/// @return Return value description needed.
                get();
            continue;
        }

/// @brief Implements if functionality.
/// @param char>(peek( Parameter description needed.
/// @return Return value description needed.
        if (std::toupper(static_cast<unsigned char>(peek())) == 'R' && pos_ + 2 < src_.size() &&
            std::toupper(static_cast<unsigned char>(src_[pos_ + 1])) == 'E' &&
            std::toupper(static_cast<unsigned char>(src_[pos_ + 2])) == 'M')
        {
            char after = (pos_ + 3 < src_.size()) ? src_[pos_ + 3] : '\0';
/// @brief Implements if functionality.
/// @param char>(after Parameter description needed.
/// @return Return value description needed.
            if (!std::isalnum(static_cast<unsigned char>(after)) && after != '$' && after != '#' &&
                after != '!' && after != '%' && after != '&')
            {
/// @brief Retrieves  value.
/// @return Return value description needed.
                get();
/// @brief Retrieves  value.
/// @return Return value description needed.
                get();
/// @brief Retrieves  value.
/// @return Return value description needed.
                get();
/// @brief Implements while functionality.
/// @param !eof( Parameter description needed.
/// @return Return value description needed.
                while (!eof() && peek() != '\n')
/// @brief Retrieves  value.
/// @return Return value description needed.
                    get();
                continue;
            }
        }

        break;
    }
}

/// @brief Lex a numeric literal including optional fraction, exponent, and type
///        suffix (<tt>%</tt>, <tt>&</tt>, <tt>!</tt>, <tt>#</tt>).
///
/// @details Consumes digits, a single decimal point, and an optional exponent
///          section before capturing trailing type designators.  The recognised
///          substring is returned verbatim so later stages can enforce precise
///          numeric semantics.  Location data is captured prior to any
///          consumption for accurate diagnostics.
/// @return Token of kind Number representing the characters consumed.
Token Lexer::lexNumber()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string s;
    bool seenDot = false;
    bool seenExp = false;
    char suffix = '\0';
/// @brief Implements if functionality.
/// @param peek( Parameter description needed.
/// @return Return value description needed.
    if (peek() == '.')
    {
        seenDot = true;
        s.push_back(get());
    }
/// @brief Implements while functionality.
/// @param char>(peek( Parameter description needed.
/// @return Return value description needed.
    while (std::isdigit(static_cast<unsigned char>(peek())))
        s.push_back(get());
/// @brief Implements if functionality.
/// @param peek( Parameter description needed.
/// @return Return value description needed.
    if (!seenDot && peek() == '.')
    {
        seenDot = true;
        s.push_back(get());
/// @brief Implements while functionality.
/// @param char>(peek( Parameter description needed.
/// @return Return value description needed.
        while (std::isdigit(static_cast<unsigned char>(peek())))
            s.push_back(get());
    }
/// @brief Implements if functionality.
/// @param (peek( Parameter description needed.
/// @return Return value description needed.
    if ((peek() == 'e' || peek() == 'E'))
    {
        seenExp = true;
        s.push_back(get());
        if (peek() == '+' || peek() == '-')
            s.push_back(get());
        while (std::isdigit(static_cast<unsigned char>(peek())))
            s.push_back(get());
    }
    if (peek() == '#' || peek() == '!' || peek() == '%' || peek() == '&')
        suffix = get();
    (void)seenDot;
    (void)seenExp;
    if (suffix != '\0')
        s.push_back(suffix);
    return {TokenKind::Number, s, loc};
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
/// @brief Implements while functionality.
/// @param char>(peek( Parameter description needed.
/// @return Return value description needed.
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
        s.push_back(std::toupper(static_cast<unsigned char>(get())));
    if (peek() == '$' || peek() == '#' || peek() == '!' || peek() == '%' || peek() == '&')
        s.push_back(std::toupper(static_cast<unsigned char>(get())));
    TokenKind kind = lookupKeyword(s);
    return {kind, s, loc};
}

/// @brief Lex a string literal delimited by double quotes.
///
/// @details Copies characters verbatim until a closing quote or end of input is
///          reached.  Escape sequences are not interpreted; instead they are
///          preserved for later interpretation by runtime helpers.  When the
///          stream terminates before a closing quote the unterminated literal is
///          returned to the parser, which is responsible for issuing an error.
/// @return String token containing characters between quotes.
Token Lexer::lexString()
{
    il::support::SourceLoc loc{file_id_, line_, column_};
    std::string s;
/// @brief Retrieves  value.
/// @return Return value description needed.
    get(); // consume opening quote
    while (!eof() && peek() != '"')
    {
        char c = get();
        if (c == '\\' && !eof())
        {
            s.push_back(c);
            s.push_back(get());
            continue;
        }
        s.push_back(c);
    }
/// @brief Implements if functionality.
/// @param peek( Parameter description needed.
/// @return Return value description needed.
    if (peek() == '"')
/// @brief Retrieves  value.
/// @return Return value description needed.
        get();
    return {TokenKind::String, s, loc};
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
/// @brief Implements skipWhitespaceAndComments functionality.
/// @return Return value description needed.
    skipWhitespaceAndComments();

/// @brief Implements if functionality.
/// @param eof( Parameter description needed.
/// @return Return value description needed.
    if (eof())
        return {TokenKind::EndOfFile, "", {file_id_, line_, column_}};

    char c = peek();

    // Handle newline explicitly so skipWhitespaceAndComments is called only once.
/// @brief Implements if functionality.
/// @param '\n' Parameter description needed.
/// @return Return value description needed.
    if (c == '\n')
    {
        il::support::SourceLoc loc{file_id_, line_, column_};
/// @brief Retrieves  value.
/// @return Return value description needed.
        get();
        return {TokenKind::EndOfLine, "\n", loc};
    }

/// @brief Implements if functionality.
/// @param char>(c Parameter description needed.
/// @return Return value description needed.
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && pos_ + 1 < src_.size() &&
         std::isdigit(static_cast<unsigned char>(src_[pos_ + 1]))))
        return lexNumber();
/// @brief Implements if functionality.
/// @param char>(c Parameter description needed.
/// @return Return value description needed.
    if (std::isalpha(static_cast<unsigned char>(c)))
        return lexIdentifierOrKeyword();
/// @brief Implements if functionality.
/// @param '"' Parameter description needed.
/// @return Return value description needed.
    if (c == '"')
        return lexString();

    il::support::SourceLoc loc{file_id_, line_, column_};
/// @brief Retrieves  value.
/// @return Return value description needed.
    get();
/// @brief Implements switch functionality.
/// @param c Parameter description needed.
/// @return Return value description needed.
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
        case '&':
            return {TokenKind::Ampersand, "&", loc};
        case '=':
            return {TokenKind::Equal, "=", loc};
        case '<':
/// @brief Implements if functionality.
/// @param peek( Parameter description needed.
/// @return Return value description needed.
            if (peek() == '>')
            {
/// @brief Retrieves  value.
/// @return Return value description needed.
                get();
                return {TokenKind::NotEqual, "<>", loc};
            }
/// @brief Implements if functionality.
/// @param peek( Parameter description needed.
/// @return Return value description needed.
            if (peek() == '=')
            {
/// @brief Retrieves  value.
/// @return Return value description needed.
                get();
                return {TokenKind::LessEqual, "<=", loc};
            }
            return {TokenKind::Less, "<", loc};
        case '>':
/// @brief Implements if functionality.
/// @param peek( Parameter description needed.
/// @return Return value description needed.
            if (peek() == '=')
            {
/// @brief Retrieves  value.
/// @return Return value description needed.
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
/// @brief Implements if functionality.
/// @param 2 Parameter description needed.
/// @return Return value description needed.
            if (pos_ >= 2)
            {
                unsigned char prev = static_cast<unsigned char>(src_[pos_ - 2]);
                prevIsDigit = std::isdigit(prev) != 0;
            }
            bool nextIsDigit = false;
/// @brief Implements if functionality.
/// @param src_.size( Parameter description needed.
/// @return Return value description needed.
            if (pos_ < src_.size())
            {
                unsigned char next = static_cast<unsigned char>(src_[pos_]);
                nextIsDigit = std::isdigit(next) != 0;
            }
/// @brief Implements if functionality.
/// @param nextIsDigit Parameter description needed.
/// @return Return value description needed.
            if (prevIsDigit && nextIsDigit)
            {
/// @brief Implements if functionality.
/// @param 1 Parameter description needed.
/// @return Return value description needed.
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
