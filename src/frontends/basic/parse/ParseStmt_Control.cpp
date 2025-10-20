//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/parse/ParseStmt_Control.cpp
// Purpose: Implements control-flow statement parselets for the BASIC parser.
// Key invariants: Maintains structured block boundaries while synchronising
//                 with StatementSequencer terminators.
// Ownership/Lifetime: Parser produces AST nodes owned by caller-provided
//                     unique_ptr wrappers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "il/io/StringEscape.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

void Parser::registerControlFlowParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordIf, &Parser::parseIfStatement);
    registry.registerHandler(TokenKind::KeywordSelect, &Parser::parseSelectCaseStatement);
    registry.registerHandler(TokenKind::KeywordWhile, &Parser::parseWhileStatement);
    registry.registerHandler(TokenKind::KeywordDo, &Parser::parseDoStatement);
    registry.registerHandler(TokenKind::KeywordFor, &Parser::parseForStatement);
    registry.registerHandler(TokenKind::KeywordNext, &Parser::parseNextStatement);
    registry.registerHandler(TokenKind::KeywordExit, &Parser::parseExitStatement);
    registry.registerHandler(TokenKind::KeywordGoto, &Parser::parseGotoStatement);
    registry.registerHandler(TokenKind::KeywordGosub, &Parser::parseGosubStatement);
    registry.registerHandler(TokenKind::KeywordReturn, &Parser::parseReturnStatement);
}

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
                elseStmt = parseIfBranchBody(line, ctxIf);
                break;
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

StmtPtr Parser::parseSelectCaseStatement()
{
    auto loc = peek().loc;
    consume(); // SELECT
    expect(TokenKind::KeywordCase);
    auto selector = parseExpression();
    Token headerEnd = expect(TokenKind::EndOfLine);

    auto stmt = std::make_unique<SelectCaseStmt>();
    stmt->loc = loc;
    stmt->selector = std::move(selector);
    stmt->range.begin = loc;
    stmt->range.end = headerEnd.loc;

    SelectDiagnoseFn diagnose = [&](il::support::SourceLoc diagLoc,
                                    uint32_t length,
                                    std::string_view message,
                                    std::string_view code)
    {
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           std::string(code),
                           diagLoc,
                           length,
                           std::string(message));
        }
        else
        {
            std::fprintf(stderr, "%.*s\n", static_cast<int>(message.size()), message.data());
        }
    };

    bool sawCaseArm = false;
    bool sawCaseElse = false;
    bool expectEndSelect = true;

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::EndOfFile))
            break;

        if (at(TokenKind::Number))
        {
            TokenKind next = peek(1).kind;
            if (next == TokenKind::KeywordCase ||
                (next == TokenKind::KeywordEnd && peek(2).kind == TokenKind::KeywordSelect))
            {
                consume();
            }
        }

        auto endResult = handleEndSelect(*stmt, sawCaseArm, expectEndSelect, diagnose);
        if (endResult.handled)
            break;

        auto elseResult = consumeCaseElse(*stmt, sawCaseArm, sawCaseElse, diagnose);
        if (elseResult.handled)
            continue;

        if (!at(TokenKind::KeywordCase))
        {
            Token unexpected = consume();
            diagnose(unexpected.loc,
                     static_cast<uint32_t>(unexpected.lexeme.size()),
                     "expected CASE or END SELECT in SELECT CASE",
                     "B0001");
            continue;
        }

        Token caseTok = peek();
        il::support::SourceLoc caseLoc = caseTok.loc;

        CaseArm arm = parseCaseArm();
        arm.range.begin = caseLoc;
        stmt->arms.push_back(std::move(arm));
        if (!stmt->arms.empty())
        {
            stmt->range.end = stmt->arms.back().range.end;
        }
        sawCaseArm = true;
    }

    if (expectEndSelect)
    {
        diagnose(loc,
                 static_cast<uint32_t>(6),
                 diag::ERR_SelectCase_MissingEndSelect.text,
                 diag::ERR_SelectCase_MissingEndSelect.id);
    }

    return stmt;
}

Parser::SelectBodyResult Parser::collectSelectBody()
{
    SelectBodyResult result;
    auto bodyCtx = statementSequencer();
    auto predicate = [&](int, il::support::SourceLoc) {
        if (at(TokenKind::KeywordCase))
            return true;
        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordSelect)
            return true;
        return false;
    };
    auto consumer = [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &info) {
        info.loc = peek().loc;
    };
    result.terminator = bodyCtx.collectStatements(predicate, consumer, result.body);
    return result;
}

Parser::SelectHandlerResult Parser::handleEndSelect(SelectCaseStmt &stmt,
                                                    bool sawCaseArm,
                                                    bool &expectEndSelect,
                                                    const SelectDiagnoseFn &diagnose)
{
    SelectHandlerResult result;
    if (!(at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordSelect))
        return result;

    result.handled = true;
    consume();
    Token selectTok = expect(TokenKind::KeywordSelect);
    stmt.range.end = selectTok.loc;
    if (!sawCaseArm)
    {
        diagnose(selectTok.loc,
                 static_cast<uint32_t>(selectTok.lexeme.size()),
                 "SELECT CASE requires at least one CASE arm",
                 "B0001");
        result.emittedDiagnostic = true;
    }
    expectEndSelect = false;
    return result;
}

Parser::SelectHandlerResult Parser::consumeCaseElse(SelectCaseStmt &stmt,
                                                    bool sawCaseArm,
                                                    bool &sawCaseElse,
                                                    const SelectDiagnoseFn &diagnose)
{
    SelectHandlerResult result;
    if (!at(TokenKind::KeywordCase) || peek(1).kind != TokenKind::KeywordElse)
        return result;

    result.handled = true;
    consume();
    const Token elseTok = expect(TokenKind::KeywordElse);

    if (sawCaseElse)
    {
        diagnose(elseTok.loc,
                 static_cast<uint32_t>(elseTok.lexeme.size()),
                 diag::ERR_SelectCase_DuplicateElse.text,
                 diag::ERR_SelectCase_DuplicateElse.id);
        result.emittedDiagnostic = true;
    }
    if (!sawCaseArm)
    {
        diagnose(elseTok.loc,
                 static_cast<uint32_t>(elseTok.lexeme.size()),
                 "CASE ELSE requires a preceding CASE arm",
                 "B0001");
        result.emittedDiagnostic = true;
    }

    Token elseEol = expect(TokenKind::EndOfLine);
    auto bodyResult = collectSelectBody();
    result.emittedDiagnostic = result.emittedDiagnostic || bodyResult.emittedDiagnostic;
    if (!sawCaseElse)
    {
        stmt.elseBody = std::move(bodyResult.body);
        stmt.range.end = elseEol.loc;
    }
    sawCaseElse = true;
    return result;
}

std::pair<std::vector<StmtPtr>, il::support::SourceLoc> Parser::parseCaseElseBody()
{
    expect(TokenKind::KeywordCase);
    expect(TokenKind::KeywordElse);
    Token elseEol = expect(TokenKind::EndOfLine);

    auto bodyResult = collectSelectBody();
    auto body = std::move(bodyResult.body);

    return {std::move(body), elseEol.loc};
}

CaseArm Parser::parseCaseArm()
{
    Token caseTok = expect(TokenKind::KeywordCase);
    CaseArm arm;
    arm.range.begin = caseTok.loc;

    bool haveEntry = false;
    while (true)
    {
        if (at(TokenKind::Identifier) && peek().lexeme == "IS")
        {
            consume(); // IS
            CaseArm::CaseRel rel;
            Token opTok = peek();
            switch (opTok.kind)
            {
                case TokenKind::Less:
                    rel.op = CaseArm::CaseRel::Op::LT;
                    break;
                case TokenKind::LessEqual:
                    rel.op = CaseArm::CaseRel::Op::LE;
                    break;
                case TokenKind::Equal:
                    rel.op = CaseArm::CaseRel::Op::EQ;
                    break;
                case TokenKind::GreaterEqual:
                    rel.op = CaseArm::CaseRel::Op::GE;
                    break;
                case TokenKind::Greater:
                    rel.op = CaseArm::CaseRel::Op::GT;
                    break;
                default:
                {
                    if (opTok.kind != TokenKind::EndOfLine)
                    {
                        if (emitter_)
                        {
                            emitter_->emit(il::support::Severity::Error,
                                           "B0001",
                                           opTok.loc,
                                           static_cast<uint32_t>(opTok.lexeme.size()),
                                           "CASE IS requires a relational operator");
                        }
                        else
                        {
                            std::fprintf(stderr, "CASE IS requires a relational operator\n");
                        }
                    }
                    goto exitCaseEntries;
                }
            }
            consume();

            int sign = 1;
            if (at(TokenKind::Plus) || at(TokenKind::Minus))
            {
                sign = at(TokenKind::Minus) ? -1 : 1;
                consume();
            }

            if (!at(TokenKind::Number))
            {
                Token bad = peek();
                if (bad.kind != TokenKind::EndOfLine)
                {
                    if (emitter_)
                    {
                        emitter_->emit(il::support::Severity::Error,
                                       "B0001",
                                       bad.loc,
                                       static_cast<uint32_t>(bad.lexeme.size()),
                                       "SELECT CASE labels must be integer literals");
                    }
                    else
                    {
                        std::fprintf(stderr, "SELECT CASE labels must be integer literals\n");
                    }
                }
                goto exitCaseEntries;
            }

            Token valueTok = consume();
            long long value = std::strtoll(valueTok.lexeme.c_str(), nullptr, 10);
            rel.rhs = static_cast<int64_t>(sign * value);
            arm.rels.push_back(rel);
            haveEntry = true;
        }
        else if (at(TokenKind::String))
        {
            const Token stringTok = peek();
            std::string decoded;
            std::string err;
            if (!il::io::decodeEscapedString(stringTok.lexeme, decoded, &err))
            {
                if (emitter_)
                {
                    emitter_->emit(il::support::Severity::Error,
                                   "B0003",
                                   stringTok.loc,
                                   static_cast<uint32_t>(stringTok.lexeme.size()),
                                   err);
                }
                else
                {
                    std::fprintf(stderr, "%s\n", err.c_str());
                }
                decoded = stringTok.lexeme;
            }
            consume();
            arm.str_labels.push_back(std::move(decoded));
            haveEntry = true;
        }
        else if (at(TokenKind::Number))
        {
            Token labelTok = consume();
            long long value = std::strtoll(labelTok.lexeme.c_str(), nullptr, 10);
            int64_t lo = static_cast<int64_t>(value);

            if (at(TokenKind::KeywordTo))
            {
                consume();
                if (!at(TokenKind::Number))
                {
                    Token bad = peek();
                    if (bad.kind != TokenKind::EndOfLine)
                    {
                        if (emitter_)
                        {
                            emitter_->emit(il::support::Severity::Error,
                                           "B0001",
                                           bad.loc,
                                           static_cast<uint32_t>(bad.lexeme.size()),
                                           "SELECT CASE labels must be integer literals");
                        }
                        else
                        {
                            std::fprintf(stderr, "SELECT CASE labels must be integer literals\n");
                        }
                    }
                    goto exitCaseEntries;
                }

                Token hiTok = consume();
                long long hiValue = std::strtoll(hiTok.lexeme.c_str(), nullptr, 10);
                arm.ranges.emplace_back(lo, static_cast<int64_t>(hiValue));
                haveEntry = true;
            }
            else
            {
                arm.labels.push_back(lo);
                haveEntry = true;
            }
        }
        else
        {
            Token bad = peek();
            if (bad.kind != TokenKind::EndOfLine)
            {
                if (emitter_)
                {
                    emitter_->emit(il::support::Severity::Error,
                                   "B0001",
                                   bad.loc,
                                   static_cast<uint32_t>(bad.lexeme.size()),
                                   "SELECT CASE labels must be integer literals");
                }
                else
                {
                    std::fprintf(stderr, "SELECT CASE labels must be integer literals\n");
                }
            }
            break;
        }

        if (at(TokenKind::Comma))
        {
            consume();
            continue;
        }
        break;
    }

exitCaseEntries:

    if (!haveEntry)
    {
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           std::string(diag::ERR_Case_EmptyLabelList.id),
                           caseTok.loc,
                           static_cast<uint32_t>(caseTok.lexeme.size()),
                           std::string(diag::ERR_Case_EmptyLabelList.text));
        }
        else
        {
            const std::string msg(diag::ERR_Case_EmptyLabelList.text);
            std::fprintf(stderr, "%s\n", msg.c_str());
        }
    }

    Token caseEol = expect(TokenKind::EndOfLine);
    arm.range.end = caseEol.loc;

    auto bodyResult = collectSelectBody();
    arm.body = std::move(bodyResult.body);

    return arm;
}

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

