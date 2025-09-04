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

bool Parser::at(TokenKind k) const
{
    return peek().kind == k;
}

const Token &Parser::peek(int n) const
{
    while (tokens_.size() <= static_cast<size_t>(n))
    {
        tokens_.push_back(lexer_.next());
    }
    return tokens_[n];
}

Token Parser::consume()
{
    Token t = peek();
    tokens_.erase(tokens_.begin());
    return t;
}

Token Parser::expect(TokenKind k, const char *what)
{
    if (!at(k))
    {
        Token t = peek();
        if (de_)
            de_->emitExpected(t.kind, k, t.loc);
        else
            std::fprintf(stderr, "expected %s, got %s\n", what, tokenKindToString(t.kind));
        syncToStmtBoundary();
        return t;
    }
    return consume();
}

void Parser::syncToStmtBoundary()
{
    while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine) && !at(TokenKind::Colon))
    {
        consume();
    }
}

} // namespace il::frontends::basic
