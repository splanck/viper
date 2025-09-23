// File: src/frontends/basic/Parser_Token.hpp
// Purpose: Token navigation helpers for BASIC parser.
// Key invariants: Buffer always holds current token.
// Ownership/Lifetime: Parser owns lexer and token buffer.
// Links: docs/codemap.md
#pragma once

/// @brief Test whether the current token matches a given kind.
/// @param k Token kind to compare against the current token.
/// @return True if the current token is of kind @p k; otherwise false.
/// @note Does not modify parser state.
bool at(TokenKind k) const;

/// @brief Look ahead without consuming tokens.
/// @param n Number of tokens ahead to inspect; 0 refers to the current token.
/// @return Reference to the token at the specified lookahead position.
/// @note Does not advance the parser or alter the token buffer.
const Token &peek(int n = 0) const;

/// @brief Consume the current token and advance.
/// @return The token that was at the current position before advancing.
/// @note Advances the parser by one token.
Token consume();

/// @brief Consume a token of the expected kind or report a mismatch.
/// @param k Required token kind.
/// @return The consumed token, even if it did not match @p k.
/// @note Advances the parser and may emit a diagnostic on mismatch.
Token expect(TokenKind k);

/// @brief Skip tokens until reaching a statement boundary.
/// @note Consumes tokens until a boundary token (e.g., newline or EOF) is encountered.
void syncToStmtBoundary();
