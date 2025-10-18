//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the parsing routines for BASIC's jump-oriented statements.  By
// grouping these routines together the parser keeps the grammar-specific logic
// for GOTO, GOSUB, and RETURN statements easy to audit without scattering
// helpers across the codebase.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements jump-oriented BASIC statement parsers.
/// @details Provides the parsing routines for GOTO, GOSUB, and RETURN statements
///          and returns heap-allocated AST nodes describing the parsed
///          constructs.

#include "frontends/basic/Parser.hpp"

#include <cstdlib>

namespace il::frontends::basic
{

/// @brief Parse a `GOTO <line>` statement.
/// @details Captures the source location for error reporting, consumes the
///          `GOTO` keyword, and then reads the numeric token that supplies the
///          target line.  The helper builds a @c GotoStmt storing both the
///          location and parsed target before returning ownership to the caller.
/// @return Owned AST node describing the goto statement.
StmtPtr Parser::parseGotoStatement()
{
    auto loc = peek().loc;
    consume(); // GOTO
    int target = std::atoi(peek().lexeme.c_str());
    expect(TokenKind::Number);
    auto stmt = std::make_unique<GotoStmt>();
    stmt->loc = loc;
    stmt->target = target;
    return stmt;
}

/// @brief Parse a `GOSUB <line>` statement.
/// @details Mirrors @ref parseGotoStatement by recording the location, consuming
///          the keyword, and converting the following numeric token into the
///          subroutine target.  The resulting @c GosubStmt captures both the
///          call site and the continuation line.
/// @return Owned AST node describing the gosub statement.
StmtPtr Parser::parseGosubStatement()
{
    auto loc = peek().loc;
    consume(); // GOSUB
    int target = std::atoi(peek().lexeme.c_str());
    expect(TokenKind::Number);
    auto stmt = std::make_unique<GosubStmt>();
    stmt->loc = loc;
    stmt->targetLine = target;
    return stmt;
}

/// @brief Parse a `RETURN [expr]` statement.
/// @details Records the source location, consumes the `RETURN` keyword, and then
///          checks for an optional trailing expression.  Parsing halts at end of
///          line or colon delimiters so chained statements remain intact.  The
///          resulting @c ReturnStmt owns the parsed expression when one exists.
/// @return Owned AST node describing the return statement.
StmtPtr Parser::parseReturnStatement()
{
    auto loc = peek().loc;
    consume(); // RETURN
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->loc = loc;
    if (!at(TokenKind::EndOfLine) && !at(TokenKind::EndOfFile) && !at(TokenKind::Colon))
        stmt->value = parseExpression();
    return stmt;
}

} // namespace il::frontends::basic

