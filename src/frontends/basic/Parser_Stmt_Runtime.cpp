//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements BASIC parser helpers for runtime/system statements.  These
// routines manage the parser's internal bookkeeping for DIM/REDIM and runtime
// error handling constructs while reporting diagnostics for malformed input.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Runtime statement parser entry points for the BASIC front end.
/// @details Defines the registration hooks and concrete parse functions for
///          statements whose semantics interact with the runtime environment,
///          such as DIM, ON ERROR, and RANDOMIZE.

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"

#include <cstdio>
#include <cstdlib>

namespace il::frontends::basic
{

/// @brief Register runtime-oriented statement parsers.
///
/// @details Associates runtime-related keywords with the corresponding member
///          function parse hooks so the front end can dispatch to them when the
///          tokens are encountered.
///
/// @param registry Dispatcher accepting mappings from keywords to parser
///                 member functions.
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

/// @brief Parse an `ON ERROR GOTO` statement.
///
/// @details Consumes the mandatory `ON ERROR GOTO` keyword sequence and expects
///          a numeric label.  The helper records whether the label resolves to
///          zero so the runtime can treat it as `ON ERROR GOTO 0`.
///
/// @return Newly allocated `OnErrorGoto` node describing the handler target.
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

/// @brief Parse an `END` statement.
///
/// @details Consumes the keyword and produces a simple terminator AST node.
///          No operands are permitted in this dialect.
///
/// @return Newly allocated `EndStmt` node with source location set.
StmtPtr Parser::parseEndStatement()
{
    auto loc = peek().loc;
    consume(); // END
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse a `RESUME` statement used in error handling.
///
/// @details Supports the `RESUME`, `RESUME NEXT`, and `RESUME <label>` forms.
///          When an explicit label follows, the parser records its numeric value
///          so semantic analysis can validate the target.
///
/// @return Newly allocated `Resume` node describing the resume mode.
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

/// @brief Parse a `DIM` declaration.
///
/// @details Handles both scalar and array forms, applying suffix typing rules
///          and optional `AS` type clauses.  When the declaration denotes an
///          array the parser tracks the name in `arrays_` for later validation.
///
/// @return Newly allocated `DimStmt` representing the declaration.
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

/// @brief Parse a `REDIM` statement.
///
/// @details Expects an identifier and dimension expression enclosed in
///          parentheses.  Successful parsing inserts the array name into the
///          parser's array tracking set so subsequent operations recognise it as
///          resizable.
///
/// @return Newly allocated `ReDimStmt` describing the resize operation.
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

/// @brief Parse a `RANDOMIZE` statement.
///
/// @details Captures the source location and parses the seed expression.  The
///          runtime expects a seed argument, so the expression is required.
///
/// @return Newly allocated `RandomizeStmt` containing the seed expression.
StmtPtr Parser::parseRandomizeStatement()
{
    auto loc = peek().loc;
    consume(); // RANDOMIZE
    auto stmt = std::make_unique<RandomizeStmt>();
    stmt->loc = loc;
    stmt->seed = parseExpression();
    return stmt;
}

/// @brief Parse a `CLS` statement.
///
/// @details Consumes the keyword and produces an AST node annotated with the
///          source location.  No additional operands are required.
///
/// @return Newly allocated `ClsStmt` node.
StmtPtr Parser::parseClsStatement()
{
    auto loc = consume().loc; // CLS
    auto stmt = std::make_unique<ClsStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse a `COLOR` statement.
///
/// @details Parses the foreground colour expression and an optional background
///          expression when a comma is present.  The AST stores both for the
///          lowering phase to consume.
///
/// @return Newly allocated `ColorStmt` containing parsed operands.
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

/// @brief Parse a `LOCATE` statement.
///
/// @details Parses the mandatory row expression and optional column expression
///          separated by a comma.  The resulting AST node preserves both values
///          for later lowering.
///
/// @return Newly allocated `LocateStmt` representing the cursor positioning.
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

