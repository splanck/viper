// File: src/frontends/basic/Parser_Token.hpp
// Purpose: Token navigation helpers for BASIC parser.
// Key invariants: Buffer always holds current token.
// Ownership/Lifetime: Parser owns lexer and token buffer.
// Links: docs/class-catalog.md
#pragma once

/// @brief Test if the current token matches kind @p k.
bool at(TokenKind k) const;
/// @brief Peek @p n tokens ahead without consuming.
const Token &peek(int n = 0) const;
/// @brief Consume and return the current token.
Token consume();
/// @brief Consume a token of kind @p k or report a mismatch.
Token expect(TokenKind k);
/// @brief Advance to the next statement boundary token.
void syncToStmtBoundary();
