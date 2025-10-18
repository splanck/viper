//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements loop-related statement parsing for the BASIC front end.  The
// helpers translate token streams into typed AST nodes and emit diagnostics for
// malformed constructs while leaving ownership of the produced nodes with the
// caller.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief BASIC loop statement parser implementations.
/// @details Each helper consumes the relevant keywords, constructs the
///          appropriate AST node, and captures diagnostics through the
///          `DiagnosticEmitter` when encountering malformed syntax.

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"

#include <cstdio>

namespace il::frontends::basic
{

/// @brief Parse a `WHILE ... WEND` loop construct.
///
/// @details Consumes the `WHILE` keyword, parses the condition expression, and
///          then delegates to `statementSequencer()` to accumulate the loop
///          body until a terminating `WEND` token is found.  The resulting AST
///          node retains the source location for downstream diagnostics.
///
/// @return Newly allocated `WhileStmt` describing the loop body and condition.
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

/// @brief Parse a `DO ... LOOP` statement with optional pre/post tests.
///
/// @details Supports the full BASIC surface syntax: an optional pre-test (`DO
///          WHILE/UNTIL`), optional post-test (`LOOP WHILE/UNTIL`), and emits a
///          diagnostic if both are present.  The body statements are collected
///          with `statementSequencer()`, and condition metadata is recorded on
///          the AST node.
///
/// @return Newly allocated `DoStmt` capturing loop structure and predicates.
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

/// @brief Parse a `FOR` loop statement.
///
/// @details Extracts the loop induction variable, starting expression, final
///          bound, and optional step.  Statements until the matching `NEXT` are
///          collected as the body.  A trailing identifier following `NEXT` is
///          consumed for compatibility but otherwise ignored.
///
/// @return Newly allocated `ForStmt` with populated control expressions.
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

/// @brief Parse a `NEXT` loop terminator.
///
/// @details Consumes the `NEXT` keyword and optionally captures the loop
///          variable name when present.  The name is stored for semantic checks
///          but does not affect parsing of subsequent statements.
///
/// @return Newly allocated `NextStmt` carrying the terminating variable name.
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

/// @brief Parse an `EXIT` statement.
///
/// @details Supports `EXIT FOR`, `EXIT WHILE`, and `EXIT DO`.  When the keyword
///          is followed by an unexpected token the parser emits a diagnostic and
///          returns a benign `EndStmt` placeholder so that downstream passes can
///          continue operating on a valid AST.
///
/// @return Either a populated `ExitStmt` or a fallback `EndStmt` on error.
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

