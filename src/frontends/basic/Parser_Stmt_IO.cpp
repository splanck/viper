//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements BASIC parser helpers for IO-related statements. The routines live
// in a dedicated translation unit so the main parser remains focused on general
// statement handling while these helpers concentrate on separator and channel
// peculiarities.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Parsing utilities for BASIC IO statements (PRINT, INPUT, OPEN, etc.).
/// @details The functions consume tokens from @ref Parser, build the appropriate
///          AST nodes, and report diagnostics through the active diagnostic
///          emitter when malformed statements are encountered.

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/Parser.hpp"

#include <cstdlib>

namespace il::frontends::basic
{

/// @brief Register parsing functions for IO-related statement keywords.
/// @details Populates the provided registry so the generic parser dispatch can
///          map BASIC keywords (PRINT, OPEN, etc.) to their specialised handler
///          methods on @ref Parser.
/// @param registry Dispatcher that maps tokens to member function pointers.
void Parser::registerIoParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordPrint, &Parser::parsePrintStatement);
    registry.registerHandler(TokenKind::KeywordWrite, &Parser::parseWriteStatement);
    registry.registerHandler(TokenKind::KeywordOpen, &Parser::parseOpenStatement);
    registry.registerHandler(TokenKind::KeywordClose, &Parser::parseCloseStatement);
    registry.registerHandler(TokenKind::KeywordSeek, &Parser::parseSeekStatement);
    registry.registerHandler(TokenKind::KeywordInput, &Parser::parseInputStatement);
}

/// @brief Parse the PRINT statement, supporting both console and channel forms.
/// @details Consumes the `PRINT` token and distinguishes between the standard
///          variant and the `PRINT #` channel form. Expressions, commas, and
///          semicolons are appended to the resulting AST node until a statement
///          terminator is encountered.
/// @return Newly allocated AST node representing the parsed statement.
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

/// @brief Parse the WRITE# statement.
/// @details Handles channel-prefixed WRITE statements, requiring a hash marker
///          and comma-separated expression list. Unlike PRINT#, WRITE# does not
///          permit null items, so expressions are parsed greedily until no more
///          commas remain.
/// @return AST node representing the WRITE# statement.
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

/// @brief Parse the OPEN statement configuring file channels.
/// @details Consumes the mode keyword, validates it against supported options,
///          expects the `AS #` channel syntax, and captures optional path and
///          channel expressions. Diagnostic hooks fire when unexpected tokens
///          are encountered.
/// @return AST node describing the OPEN statement.
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

/// @brief Parse the CLOSE statement.
/// @details Requires `CLOSE #` followed by an expression naming the channel to
///          close.
/// @return AST node describing the CLOSE statement.
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

/// @brief Parse the SEEK statement.
/// @details Expects `SEEK #` followed by the channel expression and a comma
///          separating the position expression. Both operands are parsed as
///          general expressions.
/// @return AST node describing the SEEK statement.
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

/// @brief Parse the INPUT statement, supporting prompt and variable lists.
/// @details Handles optional prompt strings, comma-separated variable lists, and
///          the channel-prefixed `INPUT #` variant. The parser emits diagnostics
///          when unsupported multi-target channel input is encountered and
///          consumes trailing tokens to recover.
/// @return AST node for the parsed INPUT statement.
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
            emitError("B0001", extra, "INPUT # with multiple targets not yet supported");
            while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine) && !at(TokenKind::Colon))
            {
                consume();
            }
        }

        return stmt;
    }
    ExprPtr prompt;
    if (at(TokenKind::String))
    {
        prompt = makeStrExpr(peek().lexeme, peek().loc);
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

/// @brief Parse the `LINE INPUT` statement that reads an entire line.
/// @details Supports the channel-prefixed form (`LINE INPUT #`) and validates
///          that the destination is a simple variable or array element. When an
///          invalid target is provided, diagnostics are emitted and a fallback
///          placeholder variable is inserted so compilation can proceed.
/// @return AST node describing the LINE INPUT statement.
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
    if (rawTarget && !is<VarExpr>(*rawTarget) && !is<ArrayExpr>(*rawTarget))
    {
        il::support::SourceLoc diagLoc = rawTarget->loc.hasLine() ? rawTarget->loc : loc;
        emitError("B0001", diagLoc, "expected variable");
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
