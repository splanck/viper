// File: src/frontends/basic/Parser_Stmt_ControlHelpers.hpp
// Purpose: Provides shared helper utilities for BASIC control-flow statement parsing.
// Key invariants: Helpers maintain parser sequencing state consistency when collecting
//                 statement blocks and optional line labels.
// Ownership/Lifetime: Helpers operate on Parser-owned StatementSequencer instances
//                     and return Parser-managed AST nodes.
// License: MIT; see LICENSE for details.
// Links: docs/codemap.md

#pragma once

#include "frontends/basic/Parser.hpp"

#include <initializer_list>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

inline void Parser::skipOptionalLineLabelAfterBreak(StatementSequencer &ctx,
                                                    std::initializer_list<TokenKind> followerKinds)
{
    if (!at(TokenKind::EndOfLine))
        return;

    ctx.skipLineBreaks();

    if (!at(TokenKind::Number))
        return;

    if (followerKinds.size() == 0)
    {
        consume();
        return;
    }

    TokenKind next = peek(1).kind;
    for (auto follower : followerKinds)
    {
        if (next == follower)
        {
            consume();
            return;
        }
    }
}

inline StmtPtr Parser::parseIfBranchBody(int line, StatementSequencer &ctx)
{
    skipOptionalLineLabelAfterBreak(ctx);
    auto stmt = parseStatement(line);
    if (stmt)
        stmt->line = line;
    return stmt;
}

namespace parser_helpers
{

inline StmtPtr buildBranchList(int line,
                               il::support::SourceLoc defaultLoc,
                               std::vector<StmtPtr> &&stmts)
{
    if (stmts.empty())
        return nullptr;
    auto list = std::make_unique<StmtList>();
    list->line = line;
    il::support::SourceLoc listLoc = defaultLoc;
    for (const auto &bodyStmt : stmts)
    {
        if (bodyStmt)
        {
            listLoc = bodyStmt->loc;
            break;
        }
    }
    list->loc = listLoc;
    list->stmts = std::move(stmts);
    return list;
}

inline std::vector<StmtPtr> collectBranchStatements(
    StatementSequencer &ctx,
    const StatementSequencer::TerminatorPredicate &predicate,
    const StatementSequencer::TerminatorConsumer &consumer)
{
    std::vector<StmtPtr> stmts;
    ctx.collectStatements(predicate, consumer, stmts);
    return stmts;
}

} // namespace parser_helpers

} // namespace il::frontends::basic
