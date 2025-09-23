// File: src/frontends/basic/Parser_Token.cpp
// Purpose: Implements token navigation helpers for BASIC parser.
// Key invariants: Buffer always holds current token.
// Ownership/Lifetime: Parser owns lexer and token buffer.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include <cstdio>

namespace il::frontends::basic
{
// -----------------------------------------------------------------------------
// Token buffer navigation
// -----------------------------------------------------------------------------

/// @brief Check if the next buffered token matches the expected kind.
/// @param k Token kind to test against the next token.
/// @return True when the buffered token is of kind @p k; false otherwise.
/// @note Uses peek to populate the buffer without consuming tokens.
bool Parser::at(TokenKind k) const
{
    return peek().kind == k;
}

/// @brief Provide lookahead into the token stream without consuming tokens.
/// @param n Lookahead distance, where 0 refers to the current token.
/// @return Reference to the token at position @p n.
/// @note Extends the buffer by reading from the lexer as needed.
const Token &Parser::peek(int n) const
{
    while (tokens_.size() <= static_cast<size_t>(n))
    {
        tokens_.push_back(lexer_.next());
    }
    return tokens_[n];
}

/// @brief Remove and return the current token.
/// @return The token currently at the front of the buffer.
/// @note Fetches the token via peek before erasing it to advance the buffer.
Token Parser::consume()
{
    Token t = peek();
    tokens_.erase(tokens_.begin());
    return t;
}

/**
 * Consume the next token when its kind matches the expected value.
 *
 * If the current token does not have kind @p k, a diagnostic is emitted (or an
 * error message is printed when no emitter is available).  Tokens are then
 * discarded until the next statement boundary so that parsing can resume
 * in a stable state.  In this error case the offending token is returned.
 *
 * @param k Expected token kind.
 * @return The matched token on success; otherwise the offending token.
 */
Token Parser::expect(TokenKind k)
{
    if (!at(k))
    {
        Token t = peek();
        if (emitter_)
        {
            emitter_->emitExpected(t.kind, k, t.loc);
        }
        else
        {
            std::fprintf(
                stderr, "expected %s, got %s\n", tokenKindToString(k), tokenKindToString(t.kind));
        }
        syncToStmtBoundary();
        return t;
    }
    return consume();
}

/**
 * Discard buffered tokens until a statement boundary is found.
 *
 * Used during error recovery, this helper consumes tokens until an end-of-line,
 * colon, or end-of-file is encountered.  It does not emit diagnostics itself
 * but allows parsing to resume at the next stable location.
 */
void Parser::syncToStmtBoundary()
{
    while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine) && !at(TokenKind::Colon))
    {
        consume();
    }
}

} // namespace il::frontends::basic
