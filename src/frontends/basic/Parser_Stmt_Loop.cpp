//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC loop statement parser. Each helper consumes the
// necessary control keywords, leverages StatementSequencer to gather nested
// bodies, and issues diagnostics when headers or terminators are malformed so
// subsequent lowering stages receive structurally sound ASTs.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"

#include <cstdio>

namespace il::frontends::basic
{

/// @brief Parse a WHILE/WEND loop into an AST node.
///
/// Reads the `WHILE` keyword, captures the condition expression, and then uses
/// StatementSequencer to collect the body until a matching `WEND`. The helper
/// records the loop location for later diagnostics but otherwise leaves error
/// reporting to the sequencer if the terminator is missing.
///
/// @return Populated @ref WhileStmt instance describing the loop.
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

/// @brief Parse the BASIC DO loop family, handling all condition placements.
///
/// Consumes the `DO` keyword, optionally records pre-test conditions, and then
/// gathers the loop body until `LOOP`. Post-test conditions are parsed when
/// present, with diagnostics emitted if both pre and post tests appear. The
/// resulting @ref DoStmt encodes the condition position and kind so later
/// passes can produce the correct control flow.
///
/// @return Newly allocated @ref DoStmt that reflects the parsed structure.
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

/// @brief Parse a FOR/NEXT loop and normalise optional STEP clauses.
///
/// Captures the loop variable, range, and optional step expression before
/// delegating to StatementSequencer to collect the loop body. If the trailing
/// `NEXT` names a variable, it is consumed to keep the parser aligned regardless
/// of matching errors.
///
/// @return Owning pointer to the constructed @ref ForStmt node.
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

/// @brief Parse a stand-alone NEXT statement used to close loops.
///
/// Reads the optional loop variable following `NEXT` so semantic analysis can
/// verify it matches an active loop. The resulting AST node captures the source
/// location and variable name (or emptiness when absent).
///
/// @return Owning pointer to a @ref NextStmt carrying the parsed metadata.
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

/// @brief Parse an EXIT statement and identify the targeted loop kind.
///
/// Consumes `EXIT` followed by an optional loop keyword, defaulting to WHILE
/// semantics when no keyword is supplied. Unexpected tokens trigger diagnostics
/// and result in an @ref EndStmt placeholder so parsing can continue without
/// cascading errors.
///
/// @return Either a configured @ref ExitStmt or a no-op statement when
///         recovery was required.
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

