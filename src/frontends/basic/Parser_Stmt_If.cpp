//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements IF statement parsing for the BASIC front-end. The helper mirrors
// the surface syntax, emitting nested AST nodes for THEN/ELSEIF/ELSE branches
// while funnelling common bookkeeping through StatementSequencer so label
// recovery and fall-through handling remain centralised.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"

#include <cstdio>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

/// @brief Parse a BASIC IF statement and construct the corresponding AST node.
///
/// Consumes the `IF` keyword, expression, and `THEN` delimiter before
/// dispatching into StatementSequencer to gather branch bodies. The helper
/// recognises multiline and single-line forms, incrementally building the
/// THEN/ELSEIF/ELSE structure while emitting diagnostics for unterminated
/// blocks. All child statements inherit the source line number supplied by the
/// caller so later passes can surface precise error locations.
///
/// @param line Line number associated with the IF token as tracked by the
///             statement sequencer.
/// @return Owning pointer to the populated @ref IfStmt node.
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

