//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Parser_Tokens.cpp
/// @brief Token buffering and error handling for the Zia parser.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Parser.hpp"

namespace il::frontends::zia
{

Parser::Parser(Lexer &lexer, il::support::DiagnosticEngine &diag) : lexer_(lexer), diag_(diag)
{
    tokens_.push_back(lexer_.next());
}

//===----------------------------------------------------------------------===//
// Token Handling
//===----------------------------------------------------------------------===//

Parser::Speculation::Speculation(Parser &parser)
    : parser_(parser), savedPos_(parser.tokenPos_), savedHasError_(parser.hasError_)
{
    ++parser_.suppressionDepth_;
}

Parser::Speculation::~Speculation()
{
    --parser_.suppressionDepth_;
    if (!committed_)
    {
        parser_.tokenPos_ = savedPos_;
        parser_.hasError_ = savedHasError_;
    }
}

const Token &Parser::peek(size_t offset)
{
    while (tokens_.size() <= tokenPos_ + offset)
    {
        tokens_.push_back(lexer_.next());
    }
    return tokens_[tokenPos_ + offset];
}

Token Parser::advance()
{
    Token cur = peek();
    ++tokenPos_;
    return cur;
}

bool Parser::check(TokenKind kind, size_t offset)
{
    return peek(offset).kind == kind;
}

bool Parser::checkIdentifierLike()
{
    // Allow identifiers and certain contextual keywords that can be used as names
    if (peek().kind == TokenKind::Identifier)
        return true;

    // These keywords can be used as identifiers in parameter/variable contexts
    switch (peek().kind)
    {
        case TokenKind::KwValue: // Common parameter name (e.g., setValue(Integer value))
            return true;
        default:
            return false;
    }
}

bool Parser::match(TokenKind kind, Token *out)
{
    if (check(kind))
    {
        Token tok = advance();
        if (out)
            *out = tok;
        return true;
    }
    return false;
}

bool Parser::expect(TokenKind kind, const char *what, Token *out)
{
    if (check(kind))
    {
        Token tok = advance();
        if (out)
            *out = tok;
        return true;
    }
    error(std::string("expected ") + what + ", got " + tokenKindToString(peek().kind));
    return false;
}

void Parser::resyncAfterError()
{
    while (!check(TokenKind::Eof))
    {
        if (check(TokenKind::Semicolon))
        {
            advance();
            return;
        }
        if (check(TokenKind::RBrace) || check(TokenKind::KwFunc) || check(TokenKind::KwValue) ||
            check(TokenKind::KwEntity) || check(TokenKind::KwInterface))
        {
            return;
        }
        advance();
    }
}

//===----------------------------------------------------------------------===//
// Error Handling
//===----------------------------------------------------------------------===//

void Parser::error(const std::string &message)
{
    errorAt(peek().loc, message);
}

void Parser::errorAt(SourceLoc loc, const std::string &message)
{
    if (suppressionDepth_ > 0)
        return;
    hasError_ = true;
    diag_.report(il::support::Diagnostic{
        il::support::Severity::Error,
        message,
        loc,
        "V2000" // Zia parser error code
    });
}

} // namespace il::frontends::zia
