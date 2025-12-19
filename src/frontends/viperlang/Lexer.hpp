//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Lexer.hpp
// Purpose: Declares the lexer for tokenizing ViperLang source code.
// Key invariants: Case-sensitive keywords; proper line/column tracking.
// Ownership/Lifetime: Lexer owns copy of source; DiagnosticEngine borrowed.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/viperlang/Token.hpp"
#include "support/diagnostics.hpp"
#include <optional>
#include <string>

namespace il::frontends::viperlang
{

/// @brief Lexer for ViperLang source code.
/// @details Transforms source text into a stream of tokens.
/// Supports C-style syntax with:
/// - // single-line comments
/// - /* multi-line comments */
/// - "string literals" with escape sequences
/// - Integer literals (decimal, hex 0x, binary 0b)
/// - Floating-point literals with optional exponent
class Lexer
{
  public:
    /// @brief Create a lexer for the given source code.
    /// @param source Source code to tokenize.
    /// @param fileId File ID for source locations.
    /// @param diag Diagnostic engine for error reporting.
    Lexer(std::string source, uint32_t fileId, il::support::DiagnosticEngine &diag);

    /// @brief Get the next token from the source.
    /// @return The next token.
    Token next();

    /// @brief Peek at the next token without consuming it.
    /// @return Reference to the next token.
    const Token &peek();

  private:
    /// @brief Get current character without consuming.
    char peekChar() const;

    /// @brief Get character at offset without consuming.
    char peekChar(size_t offset) const;

    /// @brief Consume and return current character.
    char getChar();

    /// @brief Check if at end of file.
    bool eof() const;

    /// @brief Get current source location.
    il::support::SourceLoc currentLoc() const;

    /// @brief Report an error at the given location.
    void reportError(il::support::SourceLoc loc, const std::string &message);

    /// @brief Skip whitespace and comments.
    void skipWhitespaceAndComments();

    /// @brief Skip single-line comment (// ...).
    void skipLineComment();

    /// @brief Skip multi-line comment (/* ... */).
    /// @return True if comment was properly closed.
    bool skipBlockComment();

    /// @brief Lex an identifier or keyword.
    Token lexIdentifierOrKeyword();

    /// @brief Lex a number (integer or floating-point).
    Token lexNumber();

    /// @brief Lex a string literal.
    Token lexString();

    /// @brief Lex a triple-quoted string literal.
    Token lexTripleQuotedString();

    /// @brief Process escape sequence in string.
    /// @return The escaped character, or nullopt on error.
    std::optional<char> processEscape();

    /// @brief Lookup keyword by name.
    /// @return TokenKind if keyword, nullopt if identifier.
    static std::optional<TokenKind> lookupKeyword(const std::string &name);

    std::string source_;                  ///< Source code.
    uint32_t fileId_;                     ///< File ID.
    il::support::DiagnosticEngine &diag_; ///< Diagnostic engine.
    size_t pos_ = 0;                      ///< Current position.
    uint32_t line_ = 1;                   ///< Current line.
    uint32_t column_ = 1;                 ///< Current column.
    std::optional<Token> peeked_;         ///< Peeked token cache.
};

} // namespace il::frontends::viperlang
