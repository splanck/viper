//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lexer.hpp
/// @brief Lexical analyzer (tokenizer) for the ViperLang programming language.
///
/// @details The lexer transforms source code text into a stream of tokens that
/// the parser can consume. It handles:
///
/// ## Token Categories
///
/// **Identifiers and Keywords:**
/// - Identifiers: `foo`, `myVariable`, `_private`
/// - 33 reserved keywords: `var`, `func`, `entity`, `if`, `while`, etc.
/// - Keywords are case-sensitive
///
/// **Literals:**
/// - Integers: `42`, `0xFF` (hex), `0b1010` (binary)
/// - Floating-point: `3.14`, `1e-5`, `2.5E+10`
/// - Strings: `"hello"`, with escape sequences and interpolation
/// - Triple-quoted strings: `"""multi-line"""` for verbatim text
///
/// **Operators:**
/// - Arithmetic: `+`, `-`, `*`, `/`, `%`
/// - Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
/// - Logical: `&&`, `||`, `!`
/// - Bitwise: `&`, `|`, `^`, `~`
/// - Assignment: `=`
/// - Special: `?.`, `??`, `..`, `..=`, `=>`, `->`
///
/// **Delimiters:**
/// - Brackets: `(`, `)`, `[`, `]`, `{`, `}`
/// - Punctuation: `:`, `;`, `,`, `.`, `@`
///
/// ## String Interpolation
///
/// The lexer supports string interpolation with `${...}` syntax:
/// ```
/// "Hello ${name}!"
/// ```
///
/// Interpolated strings are tokenized as:
/// 1. `StringStart`: `"Hello ${` (opens interpolation)
/// 2. Expression tokens (e.g., `name`)
/// 3. `StringEnd`: `}"` or `StringMid`: `} more text ${`
///
/// This allows the parser to handle arbitrary expressions inside strings.
///
/// ## Comments
///
/// Two comment styles are supported:
/// - Single-line: `// comment to end of line`
/// - Multi-line: `/* comment */` (can be nested)
///
/// ## Error Handling
///
/// The lexer reports errors for:
/// - Unterminated strings
/// - Invalid escape sequences
/// - Invalid numeric literals (overflow, malformed)
/// - Unterminated block comments
/// - Unexpected characters
///
/// Errors are reported through the DiagnosticEngine with location information.
///
/// ## Usage Example
///
/// ```cpp
/// DiagnosticEngine diag;
/// Lexer lexer(sourceCode, fileId, diag);
///
/// while (true) {
///     Token tok = lexer.next();
///     if (tok.kind == TokenKind::Eof)
///         break;
///     // Process token...
/// }
/// ```
///
/// @invariant Case-sensitive keyword matching.
/// @invariant Proper line/column tracking for all tokens.
/// @invariant One-token lookahead via peek().
///
/// @see Token.hpp - Token types and TokenKind enum
/// @see Parser.hpp - Consumes tokens to build AST
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/viperlang/Token.hpp"
#include "support/diagnostics.hpp"
#include <optional>
#include <string>

namespace il::frontends::viperlang
{

/// @brief Lexical analyzer for ViperLang source code.
///
/// @details Transforms source text into a stream of tokens. The lexer
/// maintains position state and supports one-token lookahead via peek().
///
/// ## Token Stream
///
/// The lexer produces tokens on demand. Each call to next() or peek()
/// advances through the source, skipping whitespace and comments.
/// The token stream ends with a TokenKind::Eof token.
///
/// ## String Interpolation State
///
/// The lexer tracks interpolation state to properly tokenize strings
/// with embedded expressions. When `${` is encountered in a string,
/// the lexer enters interpolation mode and tracks brace nesting depth
/// to know when the interpolation ends.
///
/// ## Error Recovery
///
/// On lexical errors, the lexer reports the error and returns an
/// Error token. It attempts to continue lexing from the next valid
/// position to report as many errors as possible.
///
/// @invariant pos_ <= source_.size()
/// @invariant interpolationDepth_ >= 0
/// @invariant braceDepth_.size() == interpolationDepth_
class Lexer
{
  public:
    /// @brief Create a lexer for the given source code.
    /// @param source Source code text to tokenize.
    /// @param fileId File identifier for source locations (used in error messages).
    /// @param diag Diagnostic engine for reporting lexical errors.
    ///
    /// @details The lexer takes ownership of a copy of the source string.
    /// The fileId is embedded in all source locations for this lexer.
    /// The diagnostic engine is borrowed and must outlive the lexer.
    Lexer(std::string source, uint32_t fileId, il::support::DiagnosticEngine &diag);

    /// @brief Get the next token from the source, consuming it.
    /// @return The next token in the stream.
    ///
    /// @details Skips whitespace and comments before returning the next
    /// meaningful token. Returns TokenKind::Eof when the end of source
    /// is reached. Each call advances the lexer position.
    ///
    /// @note If a token was previously peeked, this returns and clears
    /// the cached peek token.
    Token next();

    /// @brief Peek at the next token without consuming it.
    /// @return Reference to the next token.
    ///
    /// @details Returns a reference to the next token without advancing
    /// the lexer position. Multiple calls to peek() return the same token.
    /// The next call to next() will return (and consume) this token.
    ///
    /// @note The returned reference is valid until the next call to next().
    const Token &peek();

  private:
    //=========================================================================
    /// @name Character Access
    /// @brief Low-level source character access methods.
    /// @{
    //=========================================================================

    /// @brief Get current character without consuming.
    /// @return The character at the current position, or '\0' at EOF.
    ///
    /// @details Does not advance the position. Safe to call at EOF.
    char peekChar() const;

    /// @brief Get character at offset without consuming.
    /// @param offset Number of characters ahead to look.
    /// @return The character at pos_ + offset, or '\0' if past EOF.
    ///
    /// @details Used for lookahead when lexing multi-character tokens
    /// like `==`, `!=`, `..`, etc.
    char peekChar(size_t offset) const;

    /// @brief Consume and return current character.
    /// @return The character at the current position before advancing.
    ///
    /// @details Advances pos_ by one. Updates line_ and column_ tracking:
    /// - On newline: increments line_, resets column_ to 1
    /// - Otherwise: increments column_
    char getChar();

    /// @brief Check if at end of file.
    /// @return True if pos_ >= source_.size().
    bool eof() const;

    /// @brief Get current source location.
    /// @return SourceLoc with current file, line, and column.
    ///
    /// @details Used to attach location information to tokens and errors.
    il::support::SourceLoc currentLoc() const;

    /// @}
    //=========================================================================
    /// @name Error Reporting
    /// @{
    //=========================================================================

    /// @brief Report a lexical error at the given location.
    /// @param loc Source location of the error.
    /// @param message Error message describing the problem.
    ///
    /// @details Sends the error to the diagnostic engine with severity Error
    /// and error code "V1000" (ViperLang lexer errors).
    void reportError(il::support::SourceLoc loc, const std::string &message);

    /// @}
    //=========================================================================
    /// @name Whitespace and Comments
    /// @brief Methods for skipping non-token content.
    /// @{
    //=========================================================================

    /// @brief Skip whitespace and comments.
    ///
    /// @details Advances past:
    /// - Whitespace: space, tab, newline, carriage return
    /// - Line comments: `// ... newline`
    /// - Block comments: `/* ... */` (supports nesting)
    ///
    /// Called before lexing each token.
    void skipWhitespaceAndComments();

    /// @brief Skip single-line comment (// ...).
    ///
    /// @details Consumes from `//` to end of line (or EOF).
    /// The newline itself is not consumed (handled by caller).
    void skipLineComment();

    /// @brief Skip multi-line comment (/* ... */).
    /// @return True if comment was properly closed, false if unterminated.
    ///
    /// @details Supports nested block comments:
    /// ```
    /// /* outer /* nested */ still outer */
    /// ```
    /// Reports an error for unterminated comments.
    bool skipBlockComment();

    /// @}
    //=========================================================================
    /// @name Token Lexing
    /// @brief Methods for lexing specific token types.
    /// @{
    //=========================================================================

    /// @brief Lex an identifier or keyword.
    /// @return Token with kind Identifier or the appropriate keyword kind.
    ///
    /// @details Identifiers start with a letter or underscore, followed by
    /// letters, digits, or underscores. After collecting the identifier,
    /// performs case-sensitive keyword lookup using binary search on the
    /// sorted keyword table.
    ///
    /// @pre Current character is a valid identifier start (letter or `_`).
    Token lexIdentifierOrKeyword();

    /// @brief Lex a numeric literal (integer or floating-point).
    /// @return Token with kind IntegerLiteral or NumberLiteral.
    ///
    /// @details Handles:
    /// - Decimal integers: `123`, `0`
    /// - Hexadecimal: `0xFF`, `0X10`
    /// - Binary: `0b1010`, `0B11`
    /// - Floating-point: `3.14`, `.5`, `1e10`, `2.5E-3`
    ///
    /// Reports errors for:
    /// - Overflow (value too large for int64_t or double)
    /// - Missing digits after `0x` or `0b`
    /// - Missing exponent digits after `e`
    ///
    /// @pre Current character is a digit.
    Token lexNumber();

    /// @brief Lex a string literal.
    /// @return Token with kind StringLiteral, StringStart, or Error.
    ///
    /// @details Handles:
    /// - Simple strings: `"hello world"`
    /// - Escape sequences: `\n`, `\t`, `\\`, `\"`, `\$`
    /// - Triple-quoted strings (delegates to lexTripleQuotedString)
    /// - String interpolation: `"Hello ${name}!"`
    ///
    /// For interpolated strings, returns StringStart and enters
    /// interpolation mode. Subsequent tokens will be expression tokens
    /// until the matching `}` is found.
    ///
    /// Reports errors for:
    /// - Unterminated strings
    /// - Invalid escape sequences
    /// - Newlines in single-quoted strings
    ///
    /// @pre Current character is `"`.
    Token lexString();

    /// @brief Lex a triple-quoted string literal.
    /// @return Token with kind StringLiteral or Error.
    ///
    /// @details Triple-quoted strings can span multiple lines and treat
    /// most characters literally:
    /// ```
    /// """
    /// This is a
    /// multi-line string.
    /// """
    /// ```
    ///
    /// Escape sequences are still processed. The only way to include
    /// `"""` in the string is via escape: `\"\"\"`
    ///
    /// @pre Current position is at the first `"` of `"""`.
    Token lexTripleQuotedString();

    /// @brief Process an escape sequence in a string.
    /// @return The escaped character, or nullopt on invalid escape.
    ///
    /// @details Called after consuming the backslash. Handles:
    /// - `\n` → newline
    /// - `\r` → carriage return
    /// - `\t` → tab
    /// - `\\` → backslash
    /// - `\"` → double quote
    /// - `\'` → single quote
    /// - `\0` → null character
    /// - `\$` → dollar sign (for escaping interpolation)
    ///
    /// Returns nullopt for unrecognized escape sequences.
    std::optional<char> processEscape();

    /// @brief Lex the continuation of an interpolated string after '}'.
    /// @return Token with kind StringMid, StringEnd, or Error.
    ///
    /// @details Called when a `}` closes an interpolation expression.
    /// Continues lexing the string content after the interpolation.
    ///
    /// Returns:
    /// - StringEnd: If the string ends with `"`
    /// - StringMid: If another interpolation `${` is found
    /// - Error: If the string is unterminated
    Token lexInterpolatedStringContinuation();

    /// @}
    //=========================================================================
    /// @name Keyword Lookup
    /// @{
    //=========================================================================

    /// @brief Look up a keyword by name.
    /// @param name The identifier to check.
    /// @return TokenKind if the name is a keyword, nullopt for identifiers.
    ///
    /// @details Performs binary search on the sorted keyword table
    /// (33 keywords). Case-sensitive matching.
    static std::optional<TokenKind> lookupKeyword(const std::string &name);

    /// @}
    //=========================================================================
    /// @name Member Variables
    /// @{
    //=========================================================================

    /// @brief Source code being tokenized.
    /// @details The lexer owns this copy of the source text.
    std::string source_;

    /// @brief File identifier for source locations.
    /// @details Embedded in all SourceLoc values from this lexer.
    uint32_t fileId_;

    /// @brief Diagnostic engine for error reporting.
    /// @details Borrowed reference; must outlive the lexer.
    il::support::DiagnosticEngine &diag_;

    /// @brief Current position in source (0-based byte offset).
    /// @invariant pos_ <= source_.size()
    size_t pos_ = 0;

    /// @brief Current line number (1-based).
    /// @details Incremented on each newline character.
    uint32_t line_ = 1;

    /// @brief Current column number (1-based).
    /// @details Reset to 1 on newline, incremented otherwise.
    uint32_t column_ = 1;

    /// @brief Cached peeked token.
    /// @details If set, next() returns this instead of lexing a new token.
    std::optional<Token> peeked_;

    //-------------------------------------------------------------------------
    /// @name String Interpolation State
    /// @brief State tracking for nested string interpolations.
    /// @{
    //-------------------------------------------------------------------------

    /// @brief Nesting depth of string interpolations.
    /// @details 0 = not in interpolation, 1+ = in nested interpolations.
    /// Incremented when `${` is encountered, decremented when matching `}`.
    int interpolationDepth_ = 0;

    /// @brief Brace depth per interpolation level.
    /// @details Tracks nested `{...}` braces within each interpolation
    /// to distinguish them from the closing `}` of the interpolation.
    /// braceDepth_[i] is the brace count for interpolation level i+1.
    std::vector<int> braceDepth_;

    /// @}
};

} // namespace il::frontends::viperlang
