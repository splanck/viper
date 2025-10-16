// File: src/frontends/basic/Parser_Stmt_Runtime.cpp
// Purpose: Implements BASIC parser helpers for runtime/system statements.
// Key invariants: Maintains parser-owned symbol sets for arrays and procedures.
// Ownership/Lifetime: Parser retains ownership of AST nodes emitted by helpers.
// License: MIT; see LICENSE for details.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"

#include <cstdio>
#include <cstdlib>

namespace il::frontends::basic
{

void Parser::registerRuntimeParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordOn, &Parser::parseOnErrorGotoStatement);
    registry.registerHandler(TokenKind::KeywordResume, &Parser::parseResumeStatement);
    registry.registerHandler(TokenKind::KeywordEnd, &Parser::parseEndStatement);
    registry.registerHandler(TokenKind::KeywordDim, &Parser::parseDimStatement);
    registry.registerHandler(TokenKind::KeywordRedim, &Parser::parseReDimStatement);
    registry.registerHandler(TokenKind::KeywordRandomize, &Parser::parseRandomizeStatement);
    registry.registerHandler(TokenKind::KeywordCls, &Parser::parseClsStatement);
    registry.registerHandler(TokenKind::KeywordColor, &Parser::parseColorStatement);
    registry.registerHandler(TokenKind::KeywordLocate, &Parser::parseLocateStatement);
}

StmtPtr Parser::parseOnErrorGotoStatement()
{
    auto loc = peek().loc;
    consume(); // ON
    expect(TokenKind::KeywordError);
    expect(TokenKind::KeywordGoto);
    Token targetTok = peek();
    int target = std::atoi(targetTok.lexeme.c_str());
    expect(TokenKind::Number);
    auto stmt = std::make_unique<OnErrorGoto>();
    stmt->loc = loc;
    stmt->target = target;
    stmt->toZero = targetTok.kind == TokenKind::Number && target == 0;
    return stmt;
}

StmtPtr Parser::parseEndStatement()
{
    auto loc = peek().loc;
    consume(); // END
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = loc;
    return stmt;
}

StmtPtr Parser::parseResumeStatement()
{
    auto loc = peek().loc;
    consume(); // RESUME
    auto stmt = std::make_unique<Resume>();
    stmt->loc = loc;
    if (at(TokenKind::KeywordNext))
    {
        consume();
        stmt->mode = Resume::Mode::Next;
    }
    else if (!(at(TokenKind::EndOfLine) || at(TokenKind::EndOfFile) || at(TokenKind::Colon) ||
               isStatementStart(peek().kind)))
    {
        Token labelTok = peek();
        int target = std::atoi(labelTok.lexeme.c_str());
        expect(TokenKind::Number);
        stmt->mode = Resume::Mode::Label;
        stmt->target = target;
    }
    return stmt;
}

StmtPtr Parser::parseDimStatement()
{
    auto loc = peek().loc;
    consume(); // DIM
    Token nameTok = expect(TokenKind::Identifier);
    auto stmt = std::make_unique<DimStmt>();
    stmt->loc = loc;
    stmt->name = nameTok.lexeme;
    stmt->type = typeFromSuffix(nameTok.lexeme);
    if (at(TokenKind::LParen))
    {
        stmt->isArray = true;
        consume();
        stmt->size = parseExpression();
        expect(TokenKind::RParen);
        if (at(TokenKind::KeywordAs))
        {
            consume();
            stmt->type = parseTypeKeyword();
        }
        arrays_.insert(stmt->name);
    }
    else
    {
        stmt->isArray = false;
        if (at(TokenKind::KeywordAs))
        {
            consume();
            stmt->type = parseTypeKeyword();
        }
    }
    return stmt;
}

StmtPtr Parser::parseReDimStatement()
{
    auto loc = peek().loc;
    consume(); // REDIM
    Token nameTok = expect(TokenKind::Identifier);
    expect(TokenKind::LParen);
    auto size = parseExpression();
    expect(TokenKind::RParen);
    auto stmt = std::make_unique<ReDimStmt>();
    stmt->loc = loc;
    stmt->name = nameTok.lexeme;
    stmt->size = std::move(size);
    arrays_.insert(stmt->name);
    return stmt;
}

StmtPtr Parser::parseRandomizeStatement()
{
    auto loc = peek().loc;
    consume(); // RANDOMIZE
    auto stmt = std::make_unique<RandomizeStmt>();
    stmt->loc = loc;
    stmt->seed = parseExpression();
    return stmt;
}

StmtPtr Parser::parseClsStatement()
{
    auto loc = consume().loc; // CLS
    auto stmt = std::make_unique<ClsStmt>();
    stmt->loc = loc;
    return stmt;
}

StmtPtr Parser::parseColorStatement()
{
    auto loc = consume().loc; // COLOR
    auto stmt = std::make_unique<ColorStmt>();
    stmt->loc = loc;
    stmt->fg = parseExpression();
    if (at(TokenKind::Comma))
    {
        consume();
        stmt->bg = parseExpression();
    }
    return stmt;
}

StmtPtr Parser::parseLocateStatement()
{
    auto loc = consume().loc; // LOCATE
    auto stmt = std::make_unique<LocateStmt>();
    stmt->loc = loc;
    stmt->row = parseExpression();
    if (at(TokenKind::Comma))
    {
        consume();
        stmt->col = parseExpression();
    }
    return stmt;
}

} // namespace il::frontends::basic

