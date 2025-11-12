// File: src/frontends/basic/Lexer.hpp
// Purpose: Declares BASIC token lexer with single-line comment support.
// Key invariants: Current position always within source buffer.
// Ownership/Lifetime: Lexer does not own the source buffer.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/Token.hpp"
#include <string_view>

namespace il::frontends::basic
{

/// @brief Tokenizes BASIC source text into a stream of tokens.
/// @details Construct with a source buffer and file identifier, then call
/// next() repeatedly to iterate through tokens until an EOF token is returned.
class Lexer
{
  public:
    /// @brief Create a lexer over the given source buffer.
    /// @param src Source text to tokenize. The lexer does not take ownership.
    /// @param file_id Identifier of the source file for diagnostics.
    Lexer(std::string_view src, uint32_t file_id);

    /// @brief Produce the next token in the source.
    /// @return The next lexical token, or an EOF token when no characters
    /// remain.
    Token next();

  private:
    /// @brief Look at the current character without consuming it.
    /// @return The current character, or '\0' if at end of source.
    char peek() const;

    /// @brief Consume and return the current character.
    /// @return The consumed character, or '\0' if at end of source.
    char get();

    /// @brief Check whether the lexer has reached the end of the source.
    /// @return True if no characters remain, otherwise false.
    bool eof() const;

    /// @brief Skip spaces and tabs but leave newlines intact.
    void skipWhitespaceExceptNewline();

    /// @brief Skip spaces, tabs, and BASIC comments starting with `'` or REM.
    void skipWhitespaceAndComments();

    /// @brief Lex a numeric literal including optional decimal point and exponent.
    /// @details Consumes digit sequences, optional decimal fraction, optional
    ///          exponent (E/e followed by optional +/- and digits), and optional
    ///          type suffix (#, !, &, %). Returns a Number token.
    /// @return Token of kind Number with the full lexeme captured.
    Token lexNumber();

    /// @brief Lex an identifier or keyword.
    /// @details Consumes an alphabetic character followed by alphanumerics or
    ///          underscores, plus optional type suffix ($, #, !, &, %). Matches
    ///          against keyword table to determine if the token is a keyword.
    /// @return Token of kind Identifier or the corresponding KeywordXxx kind.
    Token lexIdentifierOrKeyword();

    /// @brief Lex a string literal enclosed in double quotes.
    /// @details Handles escape sequences and validates that the string is
    ///          properly terminated. Reports error for unterminated strings.
    /// @return Token of kind String with escape sequences in the lexeme.
    Token lexString();

    std::string_view src_; ///< Source code being tokenized.
    size_t pos_ = 0;       ///< Current index into the source buffer.
    uint32_t file_id_;
    uint32_t line_ = 1;   ///< 1-based line number of current character.
    uint32_t column_ = 1; ///< 1-based column number of current character.
};

} // namespace il::frontends::basic
