//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Lexer class, which performs lexical analysis of BASIC
// source code and produces a stream of tokens for the parser.
//
// The lexer is the first stage of the BASIC frontend compilation pipeline:
//   Lexer -> Parser -> AST -> Semantic -> Lowerer -> IL
//
// Key Responsibilities:
// - Tokenizes BASIC source text into lexical tokens (keywords, identifiers,
//   literals, operators, punctuation)
// - Recognizes BASIC-specific constructs including:
//   * Keywords (IF, THEN, FOR, NEXT, DIM, SUB, FUNCTION, etc.)
//   * Type suffixes (%, &, !, #, $) for integer, long, single, double, string
//   * Numeric literals (integer, floating-point, scientific notation)
//   * String literals with escape sequences
//   * Line numbers and labels
//   * Comment syntax (REM statements and ' single-line comments)
// - Maintains source location information for diagnostic reporting
// - Provides efficient single-pass scanning with minimal lookahead
//
// Design Notes:
// - The lexer does not own the source buffer; callers must ensure the buffer
//   remains valid for the lexer's lifetime
// - Character position tracking enables accurate error reporting during parsing
//   and semantic analysis
// - Whitespace (spaces, tabs) is skipped, but newlines are preserved as tokens
//   since BASIC uses line-oriented syntax
// - The lexer handles both traditional BASIC line numbers and modern label-based
//   control flow
// - Cursor management (peek/get/eof/position tracking) is inherited from
//   LexerCursor<Lexer> via CRTP, shared with the Zia frontend
//
// Usage:
//   Lexer lex(sourceText, fileId);
//   Token tok;
//   while ((tok = lex.next()).kind != TokenKind::Eof) {
//     // Process token
//   }
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/Token.hpp"
#include "frontends/common/LexerBase.hpp"
#include <string_view>

namespace il::frontends::basic {

/// @brief Tokenizes BASIC source text into a stream of tokens.
/// @details Construct with a source buffer and file identifier, then call
/// next() repeatedly to iterate through tokens until an EOF token is returned.
/// Inherits cursor management (peek/get/eof/position tracking) from
/// LexerCursor<Lexer> via CRTP.
class Lexer : public il::frontends::common::lexer_base::LexerCursor<Lexer> {
    using Base = il::frontends::common::lexer_base::LexerCursor<Lexer>;

  public:
    /// @brief Create a lexer over the given source buffer.
    /// @param src Source text to tokenize. The lexer does not take ownership.
    /// @param file_id Identifier of the source file for diagnostics.
    Lexer(std::string_view src, uint32_t file_id);

    /// @brief Produce the next token in the source.
    /// @return The next lexical token, or an EOF token when no characters
    /// remain.
    Token next();

    /// @brief Provide the source buffer to the CRTP base class.
    /// @return View of the source text being tokenized.
    [[nodiscard]] std::string_view source() const {
        return src_;
    }

  private:
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
};

} // namespace il::frontends::basic
