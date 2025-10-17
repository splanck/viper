//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC parser helpers for jump-oriented statements: GOTO,
// GOSUB, and RETURN.  These routines translate line-number targets into AST
// nodes and preserve optional resume modifiers.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parsing helpers for BASIC jump statements.
/// @details Each routine consumes the keyword and associated operands while
///          keeping track of the source location for later diagnostics.  The
///          resulting AST nodes are subsequently validated by the semantic
///          analyzer to ensure jump targets exist.

#include "frontends/basic/Parser.hpp"

#include <cstdlib>

namespace il::frontends::basic
{

/// @brief Parse the GOTO statement.
/// @details Expects a line number after the GOTO keyword.  The helper captures
///          the numeric target and source location in a @ref GotoStmt node so
///          control flow can be validated later.
/// @return AST node representing the GOTO statement.
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

/// @brief Parse the GOSUB statement.
/// @details Mirrors @ref parseGotoStatement but records that the jump is a
///          subroutine invocation, allowing the semantic analyzer to ensure a
///          RETURN exists.
/// @return AST node representing the GOSUB statement.
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

/// @brief Parse the RETURN statement.
/// @details Consumes the RETURN keyword and, when present, parses the trailing
///          expression that should be evaluated before resuming execution.  The
///          AST stores the optional value so semantic analysis can enforce the
///          dialect rules for RETURN-with-value.
/// @return AST node representing the RETURN statement.
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

