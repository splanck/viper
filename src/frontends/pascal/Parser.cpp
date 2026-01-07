//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Parser.cpp
// Purpose: Core utilities for the recursive descent parser for Viper Pascal.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Parser.hpp"

namespace il::frontends::pascal
{

//=============================================================================
// Constructor
//=============================================================================

Parser::Parser(Lexer &lexer, il::support::DiagnosticEngine &diag) : lexer_(lexer), diag_(diag)
{
    // Prime the parser with the first token
    current_ = lexer_.next();
}

//=============================================================================
// Token Handling
//=============================================================================

const Token &Parser::peek() const
{
    return current_;
}

Token Parser::advance()
{
    Token result = current_;
    current_ = lexer_.next();
    return result;
}

bool Parser::check(TokenKind kind) const
{
    return current_.kind == kind;
}

bool Parser::match(TokenKind kind)
{
    if (check(kind))
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenKind kind, const char *what)
{
    if (check(kind))
    {
        advance();
        return true;
    }
    error(std::string("expected ") + what + ", got " + tokenKindToString(current_.kind));
    return false;
}

void Parser::resyncAfterError()
{
    // Skip tokens until we hit a synchronization point
    while (!check(TokenKind::Eof))
    {
        // Synchronize on statement terminators and block keywords
        if (check(TokenKind::Semicolon) || check(TokenKind::KwEnd) || check(TokenKind::KwElse) ||
            check(TokenKind::KwUntil))
        {
            return;
        }
        advance();
    }
}

//=============================================================================
// Token Utilities
//=============================================================================

bool Parser::isKeyword(TokenKind kind)
{
    // Check if token is in the keyword range (KwAnd through KwFinalization)
    return kind >= TokenKind::KwAnd && kind <= TokenKind::KwFinalization;
}

//=============================================================================
// Error Handling
//=============================================================================

void Parser::error(const std::string &message)
{
    errorAt(current_.loc, message);
}

void Parser::errorAt(il::support::SourceLoc loc, const std::string &message)
{
    hasError_ = true;
    diag_.report(il::support::Diagnostic{il::support::Severity::Error, message, loc, ""});
}

} // namespace il::frontends::pascal
