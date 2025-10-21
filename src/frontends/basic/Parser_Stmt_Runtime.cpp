//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC front-end statement parsers that feed runtime services
// such as error handling, screen control, and dynamic memory helpers. These
// routines populate the AST with nodes that downstream lowering phases
// recognise while keeping symbol table side effects consistent with interpreter
// semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parses BASIC statements whose behaviour is provided by the runtime.
/// @details Runtime-oriented statements (DIM, ON ERROR, RANDOMIZE, etc.) require
///          special handling to maintain global interpreter state. Concentrating
///          the parsing logic in this translation unit keeps the main parser
///          definition compact and surfaces the runtime-focused invariants in a
///          single location.

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"

#include <cstdio>
#include <cstdlib>

namespace il::frontends::basic
{

/// @brief Register runtime statement handlers with the parser registry.
///
/// @details The BASIC parser maintains a dispatch table mapping initial tokens
///          to member function pointers. Runtime statements are defined in this
///          file, so the parser injects their handlers during construction to
///          ensure they participate in statement decoding alongside other
///          language forms.
///
/// @param registry Dispatch table that maps tokens to parser member functions.
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
/// @details The grammar expects `ON ERROR GOTO <number>`. The numeric label may
///          be zero to reset the error handler. The parser records the numeric
///          value verbatim and annotates the AST node with whether the handler
///          should be cleared.
///
/// @return Newly created AST node describing the `ON ERROR GOTO` statement.
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
/// @details `END` terminates program execution immediately. The parser simply
///          records the source location so later phases can emit appropriate
///          runtime calls or diagnostics when the statement appears in invalid
///          contexts.
///
/// @return AST node representing the terminating statement.
StmtPtr Parser::parseEndStatement()
{
    auto loc = peek().loc;
    consume(); // END
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse a `RESUME` statement.
///
/// @details `RESUME` either continues execution at the statement that faulted,
///          advances to the next statement, or jumps to an explicit numeric
///          label. The parser inspects optional trailing tokens to determine the
///          requested mode and stores any numeric destination for later
///          resolution.
///
/// @return AST node describing the chosen resume semantics.
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

/// @brief Parse a `DIM` statement.
///
/// @details `DIM` introduces scalar or array variables and may optionally
///          specify an explicit type via `AS <type>`. The parser normalises the
///          identifier's suffix to infer default types, records array
///          declarations in the parser's bookkeeping sets, and captures any
///          explicit bounds expression.
///
/// @return AST node representing the declaration.
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
/// @details `REDIM` resizes an existing array. The parser enforces the required
///          parentheses around the new bound, records the array name for symbol
///          tracking, and stores the new size expression for lowering.
///
/// @return AST node describing the resize operation.
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
/// @details `RANDOMIZE` seeds the runtime PRNG with the provided expression.
///          The parser builds the AST node and preserves the expression so the
///          lowering stage can evaluate it at runtime.
///
/// @return AST node for the seeding statement.
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
/// @details `CLS` clears the display. The statement has no operands, so the
///          parser primarily captures the source location to enable diagnostic
///          reporting and eventual runtime dispatch.
///
/// @return AST node representing the clear-screen request.
StmtPtr Parser::parseClsStatement()
{
    auto loc = consume().loc; // CLS
    auto stmt = std::make_unique<ClsStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse a `COLOR` statement.
///
/// @details `COLOR` accepts a foreground expression and an optional background
///          expression separated by a comma. Missing operands default to
///          runtime-defined behaviour. The parser records whichever expressions
///          are present without attempting to evaluate them.
///
/// @return AST node describing the colour change request.
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
/// @details `LOCATE` repositions the cursor. Similar to `COLOR`, the column
///          expression is optional. The parser preserves the expressions for
///          later evaluation and captures the source location for diagnostics.
///
/// @return AST node representing the cursor movement request.
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

