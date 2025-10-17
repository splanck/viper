//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC parser entry points for runtime- and environment-related
// statements such as ON ERROR, RESUME, DIM/REDIM, RANDOMIZE, CLS, COLOR, and
// LOCATE.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parsing helpers for BASIC runtime/environment statements.
/// @details Each routine consumes the keyword-specific syntax while enforcing
///          separator rules and capturing optional modifiers.  The resulting AST
///          nodes retain sufficient detail for the semantic analyzer and
///          lowering passes to request runtime helpers.

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"

#include <cstdio>
#include <cstdlib>

namespace il::frontends::basic
{

/// @brief Register runtime/environment statement handlers with the parser.
/// @details Populates the dispatcher with member pointers to each parser entry
///          point so keywords encountered during statement parsing invoke the
///          appropriate routines.
/// @param registry Registry receiving the keyword-to-handler mappings.
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

/// @brief Parse the ON ERROR GOTO statement.
/// @details Consumes the mandatory `ON ERROR GOTO <line>` sequence and records
///          the numeric target, treating `0` as the directive to disable the
///          trap.  The AST captures the location and whether trapping remains
///          active.
/// @return AST node representing the ON ERROR statement.
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

/// @brief Parse the END statement.
/// @details Consumes the END keyword and produces a simple terminator node used
///          to indicate program termination.
/// @return AST node representing the END directive.
StmtPtr Parser::parseEndStatement()
{
    auto loc = peek().loc;
    consume(); // END
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse the RESUME statement and its variants.
/// @details Handles bare RESUME, RESUME NEXT, and RESUME <label> forms.  The
///          parser records which variant appeared so execution can either retry
///          the failing line or advance to the next statement.
/// @return AST node capturing the RESUME intent.
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

/// @brief Parse the DIM statement for array declarations.
/// @details Parses a single variable name with an optional `(expr)` dimension and
///          optional AS clause.  The resulting node records the inferred type and
///          whether the declaration reserves array storage.
/// @return AST node representing the DIM statement.
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

/// @brief Parse the REDIM statement for resizing dynamic arrays.
/// @details Consumes the target array name and new bound expression, capturing
///          them in the AST so runtime lowering can request a resize.
/// @return AST node representing the REDIM statement.
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

/// @brief Parse the RANDOMIZE statement.
/// @details Consumes the RANDOMIZE keyword followed by the required seed
///          expression that initialises the RNG state.
/// @return AST node for RANDOMIZE.
StmtPtr Parser::parseRandomizeStatement()
{
    auto loc = peek().loc;
    consume(); // RANDOMIZE
    auto stmt = std::make_unique<RandomizeStmt>();
    stmt->loc = loc;
    stmt->seed = parseExpression();
    return stmt;
}

/// @brief Parse the CLS statement.
/// @details Consumes the CLS keyword and emits an AST node that requests a
///          terminal clear operation without additional parameters.
/// @return AST node for CLS.
StmtPtr Parser::parseClsStatement()
{
    auto loc = consume().loc; // CLS
    auto stmt = std::make_unique<ClsStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse the COLOR statement.
/// @details Captures foreground/background expressions, supports optional comma
///          separation, and defaults missing operands as defined by the BASIC
///          dialect.
/// @return AST node for COLOR.
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

/// @brief Parse the LOCATE statement for cursor positioning.
/// @details Parses the row expression and optional column expression, recording
///          both so the runtime helper can position the cursor accordingly.
/// @return AST node representing the LOCATE call.
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

