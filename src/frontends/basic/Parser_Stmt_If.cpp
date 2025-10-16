// File: src/frontends/basic/Parser_Stmt_If.cpp
// Purpose: Implements IF statement parsing for the BASIC parser.
// Key invariants: Ensures IF/ELSEIF/ELSE blocks are properly terminated and
//                 branch bodies honor StatementSequencer boundaries.
// Ownership/Lifetime: Parser produces AST nodes owned by caller-provided
//                     unique_ptr wrappers.
// License: MIT; see LICENSE for details.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"

#include <cstdio>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

StmtPtr Parser::parseIfStatement(int line)
{
    using parser_helpers::buildBranchList;
    using parser_helpers::collectBranchStatements;

    auto loc = peek().loc;
    consume(); // IF
    auto cond = parseExpression();
    expect(TokenKind::KeywordThen);
    auto stmt = std::make_unique<IfStmt>();
    stmt->loc = loc;
    stmt->cond = std::move(cond);

    if (at(TokenKind::EndOfLine))
    {
        enum class BlockTerminator
        {
            None,
            ElseIf,
            Else,
            EndIf,
        };

        auto ctxIf = statementSequencer();

        auto collectBranch = [&](bool allowElseBranches) -> std::pair<StmtPtr, BlockTerminator>
        {
            BlockTerminator term = BlockTerminator::None;
            auto predicate = [&](int, il::support::SourceLoc) {
                if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordIf)
                    return true;
                if (!allowElseBranches)
                    return false;
                if (at(TokenKind::KeywordElseIf))
                    return true;
                if (at(TokenKind::KeywordElse))
                    return true;
                return false;
            };
            auto consumer = [&](int lineNumber,
                                 il::support::SourceLoc,
                                 StatementSequencer::TerminatorInfo &info) {
                info.line = lineNumber;
                info.loc = peek().loc;
                if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordIf)
                {
                    Token endTok = consume();
                    info.loc = endTok.loc;
                    expect(TokenKind::KeywordIf);
                    term = BlockTerminator::EndIf;
                    return;
                }
                if (!allowElseBranches)
                    return;
                if (at(TokenKind::KeywordElseIf))
                {
                    term = BlockTerminator::ElseIf;
                    return;
                }
                if (at(TokenKind::KeywordElse))
                {
                    if (peek(1).kind == TokenKind::KeywordIf)
                    {
                        term = BlockTerminator::ElseIf;
                    }
                    else
                    {
                        term = BlockTerminator::Else;
                    }
                }
            };
            auto stmts = collectBranchStatements(ctxIf, predicate, consumer);
            return {buildBranchList(line, loc, std::move(stmts)), term};
        };

        auto [thenBranch, term] = collectBranch(true);
        stmt->then_branch = std::move(thenBranch);
        std::vector<IfStmt::ElseIf> elseifs;
        StmtPtr elseStmt;
        while (term == BlockTerminator::ElseIf)
        {
            IfStmt::ElseIf ei;
            if (at(TokenKind::KeywordElseIf))
            {
                consume();
            }
            else if (at(TokenKind::KeywordElse))
            {
                consume();
                expect(TokenKind::KeywordIf);
            }
            else
            {
                break;
            }
            ei.cond = parseExpression();
            expect(TokenKind::KeywordThen);
            auto [branchBody, nextTerm] = collectBranch(true);
            ei.then_branch = std::move(branchBody);
            elseifs.push_back(std::move(ei));
            term = nextTerm;
        }

        if (term == BlockTerminator::Else)
        {
            consume();
            auto [elseBody, endTerm] = collectBranch(false);
            elseStmt = std::move(elseBody);
            term = endTerm;
        }

        if (term != BlockTerminator::EndIf)
        {
            if (emitter_)
            {
                emitter_->emit(il::support::Severity::Error,
                               "B0004",
                               stmt->loc,
                               2,
                               "missing END IF");
            }
            else
            {
                std::fprintf(stderr, "missing END IF\n");
            }
            syncToStmtBoundary();
        }

        stmt->elseifs = std::move(elseifs);
        stmt->else_branch = std::move(elseStmt);
    }
    else
    {
        auto ctxIf = statementSequencer();
        auto thenStmt = parseIfBranchBody(line, ctxIf);
        std::vector<IfStmt::ElseIf> elseifs;
        StmtPtr elseStmt;
        while (true)
        {
            skipOptionalLineLabelAfterBreak(ctxIf,
                                            {TokenKind::KeywordElseIf, TokenKind::KeywordElse});
            if (at(TokenKind::KeywordElseIf))
            {
                consume();
                IfStmt::ElseIf ei;
                ei.cond = parseExpression();
                expect(TokenKind::KeywordThen);
                ei.then_branch = parseIfBranchBody(line, ctxIf);
                elseifs.push_back(std::move(ei));
                continue;
            }
            if (at(TokenKind::KeywordElse))
            {
                consume();
                if (at(TokenKind::KeywordIf))
                {
                    consume();
                    IfStmt::ElseIf ei;
                    ei.cond = parseExpression();
                    expect(TokenKind::KeywordThen);
                    ei.then_branch = parseIfBranchBody(line, ctxIf);
                    elseifs.push_back(std::move(ei));
                    continue;
                }
                else
                {
                    elseStmt = parseIfBranchBody(line, ctxIf);
                }
            }
            break;
        }
        stmt->then_branch = std::move(thenStmt);
        stmt->elseifs = std::move(elseifs);
        stmt->else_branch = std::move(elseStmt);
    }

    if (stmt->then_branch)
        stmt->then_branch->line = line;
    for (auto &elseif : stmt->elseifs)
    {
        if (elseif.then_branch)
            elseif.then_branch->line = line;
    }
    if (stmt->else_branch)
        stmt->else_branch->line = line;
    return stmt;
}

} // namespace il::frontends::basic

