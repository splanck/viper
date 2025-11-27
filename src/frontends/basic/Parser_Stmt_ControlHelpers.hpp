//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/Parser_Stmt_ControlHelpers.hpp
// Purpose: Provides shared helper utilities for BASIC control-flow statement parsing.
// Key invariants: Helpers maintain parser sequencing state consistency when collecting
// Ownership/Lifetime: Helpers operate on Parser-owned StatementSequencer instances
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/Parser.hpp"

#include <cstdlib>
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

    auto matchesFollowers = [&](TokenKind candidate)
    {
        if (followerKinds.size() == 0)
            return true;
        for (auto follower : followerKinds)
        {
            if (candidate == follower)
                return true;
        }
        return false;
    };

    if (at(TokenKind::Number))
    {
        Token numberTok = peek();
        if (matchesFollowers(peek(1).kind))
        {
            noteNumericLabelUsage(std::atoi(numberTok.lexeme.c_str()));
            consume();
        }
        return;
    }

    if (at(TokenKind::Identifier) && peek(1).kind == TokenKind::Colon)
    {
        Token labelTok = peek();
        TokenKind afterColon = peek(2).kind;
        if (matchesFollowers(afterColon))
        {
            consume();
            consume();
            int labelNumber = ensureLabelNumber(labelTok.lexeme);
            noteNamedLabelDefinition(labelTok, labelNumber);
        }
    }
}

inline StmtPtr Parser::parseIfBranchBody(int line, StatementSequencer &ctx)
{
    // Parse a branch body for single-line IF/ELSEIF/ELSE.
    // Collect colon-separated statements on the same logical line and stop
    // when encountering a line break or an ELSE/ELSEIF keyword so the outer
    // IF parser can consume it. This ensures all statements after THEN on a
    // single line are conditional, matching expected BASIC semantics.

    auto predicate = [&](int, il::support::SourceLoc)
    {
        // Terminate the branch body when we hit a line break, or when the
        // next token begins an ELSE/ELSEIF clause. Do not consume the tokens
        // here; the caller (parseElseChain) is responsible for that.
        if (ctx.lastSeparator() == StatementSequencer::SeparatorKind::LineBreak)
            return true;
        if (at(TokenKind::KeywordElse) || at(TokenKind::KeywordElseIf))
            return true;
        return false;
    };
    auto consumer = [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
    {
        // Intentionally do nothing: leave ELSE/ELSEIF (or EOL) for the caller.
    };

    std::vector<StmtPtr> stmts;
    ctx.collectStatements(predicate, consumer, stmts);
    if (stmts.empty())
        return nullptr;
    auto list = std::make_unique<StmtList>();
    list->line = line;
    il::support::SourceLoc listLoc = peek().loc;
    for (const auto &s : stmts)
    {
        if (s)
        {
            listLoc = s->loc;
            break;
        }
    }
    list->loc = listLoc;
    list->stmts = std::move(stmts);
    return list;
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
