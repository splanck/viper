// File: src/frontends/basic/Parser_Token.cpp
// Purpose: Implements token navigation helpers for BASIC parser.
// Key invariants: Buffer always holds current token.
// Ownership/Lifetime: Parser owns lexer and token buffer.
// Links: docs/class-catalog.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include <cstdio>

namespace il::frontends::basic
{
// -----------------------------------------------------------------------------
// Token buffer navigation
// -----------------------------------------------------------------------------

// Check whether the next buffered token matches the expected kind. This relies
// on `peek` to populate the buffer but does not consume any tokens, leaving
// `tokens_` untouched.
bool Parser::at(TokenKind k) const
{
    return peek().kind == k;
}

// Provide lookahead into the token stream. The buffer is extended by pulling
// tokens from the lexer until position `n` is available. No tokens are removed;
// only appends to `tokens_` occur.
const Token &Parser::peek(int n) const
{
    while (tokens_.size() <= static_cast<size_t>(n))
    {
        tokens_.push_back(lexer_.next());
    }
    return tokens_[n];
}

// Remove and return the current token. The token is first fetched via `peek`
// to ensure it exists, then erased from the front of `tokens_`, advancing the
// buffer by one.
Token Parser::consume()
{
    Token t = peek();
    tokens_.erase(tokens_.begin());
    return t;
}

// Expect the next token to be of kind `k`. On success the token is consumed and
// returned. On mismatch a diagnostic is emitted, `syncToStmtBoundary` is invoked
// to discard tokens up to a statement boundary, and the offending token is
// returned for context.
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

// Error-recovery helper: consume tokens until a statement boundary token is
// reached (end-of-line, colon, or end-of-file). This side effect discards any
// unexpected tokens so parsing can resume at a stable location.
void Parser::syncToStmtBoundary()
{
    while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine) && !at(TokenKind::Colon))
    {
        consume();
    }
}

} // namespace il::frontends::basic
