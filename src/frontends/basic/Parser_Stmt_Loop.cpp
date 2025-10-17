//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC parser entry points dedicated to loop statements.
// WHILE/WEND, DO/LOOP, FOR/NEXT, and EXIT dispatchers are handled here, keeping
// the token-driven parsing logic grouped with the shared helper utilities.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parsing helpers for BASIC loop constructs.
/// @details Each routine validates the surrounding syntax (terminators,
///          separators, and optional clauses) while building AST nodes that
///          preserve loop semantics such as bounds, step values, and exit
///          targets.

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"

#include <cstdio>

namespace il::frontends::basic
{

/// @brief Parse a WHILE statement and its trailing WEND.
/// @details Consumes the WHILE keyword, parses the loop condition, and then
///          collects the body using a @ref StatementSequencer until it encounters
///          the terminating WEND.  Missing terminators trigger diagnostics but
///          still yield an AST so later phases can continue operating.
/// @return AST node describing the WHILE loop.
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

/// @brief Parse DO...LOOP statements including modifiers.
/// @details Supports both pre-tested (`DO WHILE`) and post-tested (`LOOP UNTIL`)
///          forms as well as plain infinite loops.  The helper records whether
///          the condition is negated and whether the test appears at the top or
///          bottom so the lowering pass can generate the appropriate control
///          flow.
/// @return AST node representing the DO loop.
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

/// @brief Parse FOR loops with optional STEP clauses.
/// @details Reads the induction variable, start/end expressions, and an optional
///          STEP expression before handing control to the statement sequencer.
///          The routine tracks line numbers for diagnostic fidelity and ensures
///          the matching NEXT is associated with the same identifier when
///          present.
/// @return AST node representing the FOR loop.
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

/// @brief Parse the NEXT statement finalising FOR loops.
/// @details Accepts optional variable names, which are recorded to assist the
///          semantic analyzer in matching NEXT clauses to their originating FOR
///          statements.  A missing variable simply results in an empty list.
/// @return AST node wrapping the parsed NEXT clause.
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

/// @brief Parse EXIT statements targeting loops or procedures.
/// @details Consumes the EXIT keyword and optional qualifiers (e.g. EXIT FOR) so
///          the AST records the intent of the exit.  Unknown qualifiers are
///          preserved for later diagnostics to maintain error recovery.
/// @return AST node representing the EXIT statement.
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
        il::support::SourceLoc diagLoc = unexpected.kind == TokenKind::EndOfFile ? loc : unexpected.loc;
        uint32_t length = unexpected.lexeme.empty() ? 1u
                                                    : static_cast<uint32_t>(unexpected.lexeme.size());
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

