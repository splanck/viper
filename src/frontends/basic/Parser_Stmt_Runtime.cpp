//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC statement parser routines that handle runtime-oriented
// statements such as RANDOMIZE, DIM, and terminal control commands.  The
// functions augment the core parser with helpers that translate surface syntax
// into the appropriate AST nodes while maintaining the parser's bookkeeping for
// arrays, procedures, and error handling state.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/parse/StmtRegistry.hpp"

#include <cstdio>
#include <cstdlib>

/// @file
/// @brief Runtime statement parsing helpers for the BASIC front end.
/// @details These member functions extend @ref il::frontends::basic::Parser with
///          routines capable of recognising statements that interact with the
///          runtime library or terminal.  They construct the corresponding AST
///          nodes, update parser-managed symbol tables, and surface diagnostics
///          when syntax expectations are not met.

namespace il::frontends::basic
{
/// @brief Register parser callbacks for runtime-related statements.
/// @details Populates the registry with handlers for statements such as DIM,
///          RANDOMIZE, and terminal commands.  Each handler forwards to the
///          member function that performs the actual parsing work.
/// @param registry Registry that maps starting tokens to parser member functions.
void Parser::registerRuntimeParsers(parse::StmtRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordOn,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseOnErrorGotoStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordResume,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseResumeStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordEnd,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseEndStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordDim,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseDimStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordRedim,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseReDimStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordRandomize,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseRandomizeStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordCls,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseClsStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordColor,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseColorStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordLocate,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseLocateStatement));
                                 return true;
                             });
}

/// @brief Parse an @c ON ERROR GOTO statement.
/// @details Consumes the @c ON, @c ERROR, and @c GOTO keywords, parses the
///          numeric label target, and builds an @ref OnErrorGoto AST node.  The
///          helper records whether the statement targets line zero so the
///          lowerer can emit a @c RESUME 0 semantic.
/// @return Newly constructed statement node.
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

/// @brief Parse an @c END statement.
/// @details Consumes the @c END keyword and emits an @ref EndStmt node anchored
///          at the current source location.  No operands or trailing tokens are
///          required for this statement form.
/// @return Newly constructed statement node.
StmtPtr Parser::parseEndStatement()
{
    auto loc = peek().loc;
    consume(); // END
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse a @c RESUME statement.
/// @details Handles the optional @c NEXT keyword or numeric label.  When neither
///          is present the statement resumes at the point of the original error.
/// @return Newly constructed statement node.
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

/// @brief Parse a @c DIM statement.
/// @details Captures the declared name, optional array bounds, and optional type
///          annotation.  Array declarations update the parser's array tracking so
///          later phases can generate runtime allocation requests.
/// @return Newly constructed statement node.
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

/// @brief Parse a @c REDIM statement.
/// @details Re-sizes an existing array and records the declaration in the parser's
///          array set so later passes know the symbol represents an array.
/// @return Newly constructed statement node.
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

/// @brief Parse a @c RANDOMIZE statement.
/// @details Constructs a @ref RandomizeStmt capturing the optional seed
///          expression, enabling deterministic seeding when present.
/// @return Newly constructed statement node.
StmtPtr Parser::parseRandomizeStatement()
{
    auto loc = peek().loc;
    consume(); // RANDOMIZE
    auto stmt = std::make_unique<RandomizeStmt>();
    stmt->loc = loc;
    stmt->seed = parseExpression();
    return stmt;
}

/// @brief Parse a @c CLS statement.
/// @details Consumes the @c CLS keyword and emits a @ref ClsStmt node that clears
///          the terminal when executed.
/// @return Newly constructed statement node.
StmtPtr Parser::parseClsStatement()
{
    auto loc = consume().loc; // CLS
    auto stmt = std::make_unique<ClsStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse a @c COLOR statement.
/// @details Recognises the required foreground expression and optional
///          background expression separated by a comma.
/// @return Newly constructed statement node.
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

/// @brief Parse a @c LOCATE statement.
/// @details Parses the required row expression and optional column expression,
///          allowing BASIC programs to reposition the terminal cursor.
/// @return Newly constructed statement node.
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
