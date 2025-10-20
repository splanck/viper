//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/parse/ParseStmt_IO.cpp
// Purpose: Implements IO-related statement parselets for the BASIC parser.
// Key invariants: Maintains parser lookahead synchronisation when consuming
//                 separators and channel specifiers.
// Ownership/Lifetime: Parser retains ownership of tokens and produced AST nodes.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"

#include <cstdio>
#include <cstdlib>

namespace il::frontends::basic
{

void Parser::registerIoParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordPrint, &Parser::parsePrintStatement);
    registry.registerHandler(TokenKind::KeywordWrite, &Parser::parseWriteStatement);
    registry.registerHandler(TokenKind::KeywordOpen, &Parser::parseOpenStatement);
    registry.registerHandler(TokenKind::KeywordClose, &Parser::parseCloseStatement);
    registry.registerHandler(TokenKind::KeywordSeek, &Parser::parseSeekStatement);
    registry.registerHandler(TokenKind::KeywordInput, &Parser::parseInputStatement);
    registry.registerHandler(TokenKind::KeywordLine, &Parser::parseLineInputStatement);
}

StmtPtr Parser::parsePrintStatement()
{
    auto loc = peek().loc;
    consume(); // PRINT
    if (at(TokenKind::Hash))
    {
        consume();
        auto stmt = std::make_unique<PrintChStmt>();
        stmt->loc = loc;
        stmt->mode = PrintChStmt::Mode::Print;
        stmt->channelExpr = parseExpression();
        stmt->trailingNewline = true;
        if (at(TokenKind::Comma))
        {
            consume();
            while (true)
            {
                if (at(TokenKind::EndOfLine) || at(TokenKind::EndOfFile) || at(TokenKind::Colon))
                    break;
                if (isStatementStart(peek().kind))
                    break;
                stmt->args.push_back(parseExpression());
                if (!at(TokenKind::Comma))
                    break;
                consume();
            }
        }
        return stmt;
    }
    auto stmt = std::make_unique<PrintStmt>();
    stmt->loc = loc;
    while (!at(TokenKind::EndOfLine) && !at(TokenKind::EndOfFile) && !at(TokenKind::Colon))
    {
        TokenKind k = peek().kind;
        if (isStatementStart(k))
            break;
        if (at(TokenKind::Comma))
        {
            consume();
            stmt->items.push_back(PrintItem{PrintItem::Kind::Comma, nullptr});
            continue;
        }
        if (at(TokenKind::Semicolon))
        {
            consume();
            stmt->items.push_back(PrintItem{PrintItem::Kind::Semicolon, nullptr});
            continue;
        }
        stmt->items.push_back(PrintItem{PrintItem::Kind::Expr, parseExpression()});
    }
    return stmt;
}

StmtPtr Parser::parseWriteStatement()
{
    auto loc = peek().loc;
    consume(); // WRITE
    expect(TokenKind::Hash);
    auto stmt = std::make_unique<PrintChStmt>();
    stmt->loc = loc;
    stmt->mode = PrintChStmt::Mode::Write;
    stmt->trailingNewline = true;
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    while (true)
    {
        stmt->args.push_back(parseExpression());
        if (!at(TokenKind::Comma))
            break;
        consume();
    }
    return stmt;
}

StmtPtr Parser::parseOpenStatement()
{
    auto loc = peek().loc;
    consume(); // OPEN
    auto stmt = std::make_unique<OpenStmt>();
    stmt->loc = loc;
    stmt->pathExpr = parseExpression();
    expect(TokenKind::KeywordFor);
    if (at(TokenKind::KeywordInput))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Input;
    }
    else if (at(TokenKind::KeywordOutput))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Output;
    }
    else if (at(TokenKind::KeywordAppend))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Append;
    }
    else if (at(TokenKind::KeywordBinary))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Binary;
    }
    else if (at(TokenKind::KeywordRandom))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Random;
    }
    else
    {
        Token unexpected = consume();
        if (emitter_)
        {
            emitter_->emitExpected(unexpected.kind, TokenKind::KeywordInput, unexpected.loc);
        }
    }
    expect(TokenKind::KeywordAs);
    expect(TokenKind::Hash);
    stmt->channelExpr = parseExpression();
    return stmt;
}

StmtPtr Parser::parseCloseStatement()
{
    auto loc = peek().loc;
    consume(); // CLOSE
    auto stmt = std::make_unique<CloseStmt>();
    stmt->loc = loc;
    expect(TokenKind::Hash);
    stmt->channelExpr = parseExpression();
    return stmt;
}

StmtPtr Parser::parseSeekStatement()
{
    auto loc = peek().loc;
    consume(); // SEEK
    auto stmt = std::make_unique<SeekStmt>();
    stmt->loc = loc;
    expect(TokenKind::Hash);
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    stmt->positionExpr = parseExpression();
    return stmt;
}

StmtPtr Parser::parseInputStatement()
{
    auto loc = peek().loc;
    consume(); // INPUT
    if (at(TokenKind::Hash))
    {
        consume();
        Token channelTok = expect(TokenKind::Number);
        int channel = std::atoi(channelTok.lexeme.c_str());
        expect(TokenKind::Comma);
        Token targetTok = expect(TokenKind::Identifier);
        auto stmt = std::make_unique<InputChStmt>();
        stmt->loc = loc;
        stmt->channel = channel;
        stmt->target.name = targetTok.lexeme;
        stmt->target.loc = targetTok.loc;

        if (at(TokenKind::Comma))
        {
            Token extra = peek();
            if (emitter_)
            {
                emitter_->emit(il::support::Severity::Error,
                               "B0001",
                               extra.loc,
                               1,
                               "INPUT # with multiple targets not yet supported");
            }
            else
            {
                std::fprintf(stderr, "INPUT # with multiple targets not yet supported\n");
            }
            syncToStmtBoundary();
        }
        return stmt;
    }

    ExprPtr prompt;
    if (at(TokenKind::String))
    {
        auto s = std::make_unique<StringExpr>();
        s->loc = peek().loc;
        s->value = peek().lexeme;
        prompt = std::move(s);
        consume();
        expect(TokenKind::Comma);
    }
    auto stmt = std::make_unique<InputStmt>();
    stmt->loc = loc;
    stmt->prompt = std::move(prompt);

    Token nameTok = expect(TokenKind::Identifier);
    stmt->vars.push_back(nameTok.lexeme);

    while (at(TokenKind::Comma))
    {
        consume();
        Token nextTok = expect(TokenKind::Identifier);
        stmt->vars.push_back(nextTok.lexeme);
    }

    return stmt;
}

StmtPtr Parser::parseLineInputStatement()
{
    auto loc = peek().loc;
    consume(); // LINE
    expect(TokenKind::KeywordInput);
    expect(TokenKind::Hash);
    auto stmt = std::make_unique<LineInputChStmt>();
    stmt->loc = loc;
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    auto target = parseArrayOrVar();
    Expr *rawTarget = target.get();
    if (rawTarget && !dynamic_cast<VarExpr *>(rawTarget) && !dynamic_cast<ArrayExpr *>(rawTarget))
    {
        il::support::SourceLoc diagLoc = rawTarget->loc.isValid() ? rawTarget->loc : loc;
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           diagLoc,
                           1,
                           "expected variable");
        }
        auto fallback = std::make_unique<VarExpr>();
        fallback->loc = diagLoc;
        stmt->targetVar = std::move(fallback);
    }
    else
    {
        stmt->targetVar = std::move(target);
    }
    return stmt;
}

} // namespace il::frontends::basic

