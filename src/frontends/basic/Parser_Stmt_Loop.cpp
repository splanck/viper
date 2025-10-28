//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Parser_Stmt_Loop.cpp
// Purpose: Implement loop-related statement parsing for the BASIC parser.
// Key invariants: Ensures loop headers and terminators are matched and
//                 diagnostics cover invalid configurations.
// Ownership/Lifetime: Parser creates AST nodes managed by caller-owned
//                     std::unique_ptr wrappers.
// Links: docs/codemap.md, docs/basic-language.md#loops
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/StmtNodesAll.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"

#include <cstdio>

namespace il::frontends::basic
{

/// @brief Parse a `WHILE ... WEND` loop statement.
///
/// @details Consumes the `WHILE` keyword, parses the condition expression, and
///          delegates to the statement sequencer to collect the body until the
///          matching `WEND`.  The resulting AST node owns the body statements
///          and records the loop header location for diagnostics.
///
/// @return Newly allocated @ref WhileStmt representing the loop.
StmtPtr Parser::parseWhileStatement()
{
    auto loc = peek().loc;
    consume(); // WHILE
    auto cond = parseExpression();
    auto stmt = std::make_unique<WhileStmt>();
    stmt->loc = loc;
    stmt->cond = std::move(cond);
    auto ctxWhile = statementSequencer();
    ctxWhile.collectStatements(TokenKind::KeywordWend, stmt->body);
    return stmt;
}

/// @brief Parse the flexible `DO` loop family.
///
/// @details Supports pre-test (`DO WHILE`/`DO UNTIL`) and post-test (`LOOP
///          WHILE`/`LOOP UNTIL`) forms, reporting diagnostics when both are
///          specified simultaneously.  The body is gathered until the closing
///          `LOOP`, and optional conditions are stored on the AST node along
///          with their source locations.
///
/// @return Newly allocated @ref DoStmt capturing the loop semantics.
StmtPtr Parser::parseDoStatement()
{
    auto loc = peek().loc;
    consume(); // DO
    auto stmt = std::make_unique<DoStmt>();
    stmt->loc = loc;

    bool hasPreTest = false;
    if (at(TokenKind::KeywordWhile) || at(TokenKind::KeywordUntil))
    {
        hasPreTest = true;
        Token testTok = consume();
        stmt->testPos = DoStmt::TestPos::Pre;
        stmt->condKind = testTok.kind == TokenKind::KeywordWhile ? DoStmt::CondKind::While
                                                                 : DoStmt::CondKind::Until;
        stmt->cond = parseExpression();
    }

    auto ctxDo = statementSequencer();
    ctxDo.collectStatements(TokenKind::KeywordLoop, stmt->body);

    bool hasPostTest = false;
    Token postTok{};
    DoStmt::CondKind postKind = DoStmt::CondKind::None;
    ExprPtr postCond;
    if (at(TokenKind::KeywordWhile) || at(TokenKind::KeywordUntil))
    {
        hasPostTest = true;
        postTok = consume();
        postKind = postTok.kind == TokenKind::KeywordWhile ? DoStmt::CondKind::While
                                                           : DoStmt::CondKind::Until;
        postCond = parseExpression();
    }

    if (hasPreTest && hasPostTest)
    {
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           postTok.loc,
                           static_cast<uint32_t>(postTok.lexeme.size()),
                           "DO loop cannot have both pre and post conditions");
        }
        else
        {
            std::fprintf(stderr, "DO loop cannot have both pre and post conditions\n");
        }
    }
    else if (hasPostTest)
    {
        stmt->testPos = DoStmt::TestPos::Post;
        stmt->condKind = postKind;
        stmt->cond = std::move(postCond);
    }

    return stmt;
}

/// @brief Parse a `FOR` counting loop.
///
/// @details Captures the iteration variable, start/end expressions, and
///          optional `STEP` expression.  Statements are collected until the
///          matching `NEXT`, which may optionally repeat the loop variable for
///          clarity.  The resulting AST node records the source location for
///          diagnostics.
///
/// @return Newly allocated @ref ForStmt describing the loop.
StmtPtr Parser::parseForStatement()
{
    auto loc = peek().loc;
    consume(); // FOR
    auto stmt = std::make_unique<ForStmt>();
    stmt->loc = loc;
    Token varTok = expect(TokenKind::Identifier);
    stmt->var = varTok.lexeme;
    expect(TokenKind::Equal);
    stmt->start = parseExpression();
    expect(TokenKind::KeywordTo);
    stmt->end = parseExpression();
    if (at(TokenKind::KeywordStep))
    {
        consume();
        stmt->step = parseExpression();
    }
    auto ctxFor = statementSequencer();
    ctxFor.collectStatements(TokenKind::KeywordNext, stmt->body);
    if (at(TokenKind::Identifier))
    {
        consume();
    }
    return stmt;
}

/// @brief Parse a standalone `NEXT` terminator.
///
/// @details Recognises the optional loop variable and records it for semantic
///          checks.  The node is primarily used during validation to ensure
///          `FOR` loops are properly nested.
///
/// @return Newly allocated @ref NextStmt capturing the terminator.
StmtPtr Parser::parseNextStatement()
{
    auto loc = peek().loc;
    consume(); // NEXT
    std::string name;
    if (at(TokenKind::Identifier))
    {
        name = peek().lexeme;
        consume();
    }
    auto stmt = std::make_unique<NextStmt>();
    stmt->loc = loc;
    stmt->var = std::move(name);
    return stmt;
}

/// @brief Parse an `EXIT` statement for breaking out of loops.
///
/// @details Accepts an optional loop-kind keyword (`FOR`, `WHILE`, or `DO`).
///          When omitted the statement defaults to `EXIT WHILE` for
///          compatibility.  Invalid keywords trigger diagnostics and the parser
///          synthesises a no-op sentinel so compilation can continue.
///
/// @return Newly allocated @ref ExitStmt describing the exit semantics.
StmtPtr Parser::parseExitStatement()
{
    auto loc = peek().loc;
    consume(); // EXIT

    ExitStmt::LoopKind kind = ExitStmt::LoopKind::While;
    if (at(TokenKind::KeywordFor))
    {
        consume();
        kind = ExitStmt::LoopKind::For;
    }
    else if (at(TokenKind::KeywordWhile))
    {
        consume();
        kind = ExitStmt::LoopKind::While;
    }
    else if (at(TokenKind::KeywordDo))
    {
        consume();
        kind = ExitStmt::LoopKind::Do;
    }
    else
    {
        Token unexpected = peek();
        il::support::SourceLoc diagLoc =
            unexpected.kind == TokenKind::EndOfFile ? loc : unexpected.loc;
        uint32_t length =
            unexpected.lexeme.empty() ? 1u : static_cast<uint32_t>(unexpected.lexeme.size());
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B0002",
                           diagLoc,
                           length,
                           "expected FOR, WHILE, or DO after EXIT");
        }
        else
        {
            std::fprintf(stderr, "expected FOR, WHILE, or DO after EXIT\n");
        }
        auto noop = std::make_unique<EndStmt>();
        noop->loc = loc;
        return noop;
    }

    auto stmt = std::make_unique<ExitStmt>();
    stmt->loc = loc;
    stmt->kind = kind;
    return stmt;
}

} // namespace il::frontends::basic
