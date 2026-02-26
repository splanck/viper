//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Parser_Token.cpp
// Purpose: Implement token buffer management utilities for the BASIC parser.
// Links: docs/basic-language.md#parser
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides lookahead, consumption, and error recovery helpers for the BASIC parser.
/// @details Keeping the token-buffer mechanics here keeps the main parser
///          translation units focused on grammar productions while centralising
///          boundary synchronisation policies.

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include <cstdio>

namespace il::frontends::basic
{
// -----------------------------------------------------------------------------
// Token buffer navigation
// -----------------------------------------------------------------------------

/// @brief Check if the next buffered token matches the expected kind.
/// @details Uses @ref peek to ensure the buffer contains at least one token and
///          then compares its kind against @p k without consuming it.  Provides a
///          lightweight predicate used throughout the parser to guard optional
///          productions.
/// @param k Token kind to test against the next token.
/// @return True when the buffered token is of kind @p k; false otherwise.
bool Parser::at(TokenKind k) const
{
    return peek().kind == k;
}

/// @brief Provide lookahead into the token stream without consuming tokens.
/// @details Extends the buffered window by repeatedly querying the lexer until
///          the requested lookahead index exists.  Tokens remain in the buffer so
///          subsequent calls can reuse them.
/// @param n Lookahead distance, where 0 refers to the current token.
/// @return Reference to the token at position @p n.
const Token &Parser::peek(int n) const
{
    while (tokens_.size() <= static_cast<size_t>(n))
    {
        tokens_.push_back(lexer_.next());
    }
    return tokens_[n];
}

/// @brief Remove and return the current token.
/// @details Fetches the token via @ref peek to ensure the buffer contains a
///          value, then erases it from the front so subsequent reads observe the
///          next token.
/// @return The token currently at the front of the buffer.
Token Parser::consume()
{
    Token t = peek();
    tokens_.erase(tokens_.begin());
    return t;
}

/// @brief Consume the next token when its kind matches the expected value.
/// @details When the lookahead token does not match @p k, the helper emits a
///          diagnostic (or logs a fallback message) and then calls
///          @ref syncToStmtBoundary to recover.  The offending token is returned
///          so callers can decide how to proceed.
/// @param k Expected token kind.
/// @return The matched token on success; otherwise the offending token.
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

/// @brief Discard buffered tokens until a statement boundary is found.
/// @details Used during error recovery, the method consumes tokens until it
///          encounters an end-of-line, colon, or end-of-file token.  It avoids
///          emitting additional diagnostics so callers remain in control of
///          messaging while ensuring the parser resumes at a stable location.
void Parser::syncToStmtBoundary()
{
    // Bounded token consumption prevents compiler hang on pathological input.
    constexpr unsigned kMaxResyncTokens = 10000;
    unsigned consumed = 0;

    while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine) && !at(TokenKind::Colon) &&
           consumed < kMaxResyncTokens)
    {
        consume();
        ++consumed;
    }
}

} // namespace il::frontends::basic
