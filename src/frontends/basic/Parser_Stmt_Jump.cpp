//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements jump-oriented BASIC statement parsers.
/// @details Provides the parsing routines for GOTO, GOSUB, and RETURN statements
/// and returns heap-allocated AST nodes describing the parsed constructs.

#include "frontends/basic/Parser.hpp"

#include <cstdlib>

namespace il::frontends::basic
{

/// @brief Parse a `GOTO <line>` statement.
/// @details Consumes the `GOTO` keyword followed by a numeric token representing
/// the destination line number, constructing a @c GotoStmt with the captured
/// metadata.
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
/// @details Reads the line number after the @c GOSUB keyword and materialises a
/// @c GosubStmt describing both the call site location and continuation target.
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
/// @details Captures an optional trailing expression that produces the return
/// value when present. Parsing stops at end-of-line delimiters so chained
/// statements remain intact.
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

