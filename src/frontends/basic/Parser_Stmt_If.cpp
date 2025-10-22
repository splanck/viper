//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements parsing of BASIC IF/ELSEIF/ELSE constructs.  The helpers in this
// file coordinate statement sequencing, handle multi-branch termination, and
// produce the structured AST representation consumed by semantic analysis and
// lowering.  Keeping the logic out-of-line isolates the complex branch
// collection workflow from the core parser header.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"

#include <cstdio>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

/// @brief Parse the leading `IF ... THEN` clause.
///
/// @details The parser records the line number and source location, consumes
///          the `IF` token, parses the condition expression, and requires the
///          `THEN` keyword.  The resulting @ref IfParseState owns the partially
///          constructed AST node and is handed to @ref parseIfBlock for branch
///          collection.
///
/// @param line Line number computed by the statement sequencer.
/// @return Parser state capturing the header information and AST under
///         construction.
Parser::IfParseState Parser::parseIfHeader(int line)
{
    IfParseState state;
    state.line = line;
    state.loc = peek().loc;
    consume(); // IF
    auto cond = parseExpression();
    expect(TokenKind::KeywordThen);

    state.stmt = std::make_unique<IfStmt>();
    state.stmt->loc = state.loc;
    state.stmt->cond = std::move(cond);
    return state;
}

/// @brief Collect the branches associated with an IF statement.
///
/// @details Using @ref StatementSequencer, the helper repeatedly gathers
///          statements until it encounters a terminating keyword (`ELSEIF`,
///          `ELSE`, or `END IF`).  Each branch body is wrapped in a statement
///          list node via `buildBranchList`, and ELSEIF arms are recorded with
///          their own conditions.  The routine continues until the IF structure
///          is properly closed, populating the AST stored in @p state.
///
/// @param state Parser state initialised by @ref parseIfHeader.
void Parser::parseIfBlock(IfParseState &state)
{
    using parser_helpers::buildBranchList;
    using parser_helpers::collectBranchStatements;

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
            if (!at(TokenKind::KeywordElse))
                return;
            if (peek(1).kind == TokenKind::KeywordIf)
            {
                term = BlockTerminator::ElseIf;
                return;
            }
            term = BlockTerminator::Else;
        };
        auto stmts = collectBranchStatements(ctxIf, predicate, consumer);
        return {buildBranchList(state.line, state.loc, std::move(stmts)), term};
    };

    auto [thenBranch, term] = collectBranch(true);
    state.stmt->then_branch = std::move(thenBranch);
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
                           state.stmt->loc,
                           2,
                           "missing END IF");
        }
        else
        {
            std::fprintf(stderr, "missing END IF\n");
        }
        syncToStmtBoundary();
    }

    state.stmt->elseifs = std::move(elseifs);
    state.stmt->else_branch = std::move(elseStmt);
}

void Parser::parseElseChain(IfParseState &state)
{
    auto ctxIf = statementSequencer();
    auto thenStmt = parseIfBranchBody(state.line, ctxIf);
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
            ei.then_branch = parseIfBranchBody(state.line, ctxIf);
            elseifs.push_back(std::move(ei));
            continue;
        }

        if (!at(TokenKind::KeywordElse))
            break;

        consume();
        if (at(TokenKind::KeywordIf))
        {
            consume();
            IfStmt::ElseIf ei;
            ei.cond = parseExpression();
            expect(TokenKind::KeywordThen);
            ei.then_branch = parseIfBranchBody(state.line, ctxIf);
            elseifs.push_back(std::move(ei));
            continue;
        }

        elseStmt = parseIfBranchBody(state.line, ctxIf);
        break;
    }

    state.stmt->then_branch = std::move(thenStmt);
    state.stmt->elseifs = std::move(elseifs);
    state.stmt->else_branch = std::move(elseStmt);
}

StmtPtr Parser::parseIfStatement(int line)
{
    auto state = parseIfHeader(line);

    if (at(TokenKind::EndOfLine))
    {
        parseIfBlock(state);
    }
    else
    {
        parseElseChain(state);
    }

    if (state.stmt->then_branch)
        state.stmt->then_branch->line = line;
    for (auto &elseif : state.stmt->elseifs)
    {
        if (elseif.then_branch)
            elseif.then_branch->line = line;
    }
    if (state.stmt->else_branch)
        state.stmt->else_branch->line = line;
    return std::move(state.stmt);
}

} // namespace il::frontends::basic

