//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the control-flow statement parsing helpers for the BASIC parser.
// The routines in this translation unit rely on StatementParseDriver to share
// common sequencing behaviour for multi-line constructs such as IF blocks,
// loops, and SELECT CASE statements.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "il/io/StringEscape.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

class StatementParseDriver
{
  public:
    StatementParseDriver(Parser &parser, int line)
        : parser_(parser), line_(line), sequencer_(parser.statementSequencer())
    {
    }

    StatementSequencer &sequencer()
    {
        return sequencer_;
    }

    int line() const
    {
        return line_;
    }

    void skipOptionalLineLabel(std::initializer_list<TokenKind> followers = {})
    {
        parser_.skipOptionalLineLabelAfterBreak(sequencer_, followers);
    }

    StmtPtr parseBranchBody()
    {
        parser_.skipOptionalLineLabelAfterBreak(sequencer_);
        auto stmt = parser_.parseStatement(line_);
        if (stmt)
            stmt->line = line_;
        return stmt;
    }

    StmtPtr wrapList(std::vector<StmtPtr> &&stmts, il::support::SourceLoc fallback) const
    {
        if (stmts.empty())
            return nullptr;
        auto list = std::make_unique<StmtList>();
        list->line = line_;
        il::support::SourceLoc listLoc = fallback;
        for (const auto &bodyStmt : stmts)
        {
            if (bodyStmt && bodyStmt->loc.isValid())
            {
                listLoc = bodyStmt->loc;
                break;
            }
        }
        if (!listLoc.isValid())
            listLoc = fallback;
        list->loc = listLoc;
        list->stmts = std::move(stmts);
        return list;
    }

  private:
    Parser &parser_;
    int line_ = 0;
    StatementSequencer sequencer_;
};

namespace control_flow
{
StmtPtr parseIf(Parser &parser, int line)
{
    auto loc = parser.peek().loc;
    parser.consume();
    auto cond = parser.parseExpression();
    parser.expect(TokenKind::KeywordThen);

    auto stmt = std::make_unique<IfStmt>();
    stmt->loc = loc;
    stmt->cond = std::move(cond);

    if (parser.at(TokenKind::EndOfLine))
    {
        enum class BlockTerminator
        {
            None,
            ElseIf,
            Else,
            EndIf,
        };

        StatementParseDriver driver(parser, line);

        auto makeBranchBody = [&](std::vector<StmtPtr> &&stmts) {
            return driver.wrapList(std::move(stmts), loc);
        };

        auto collectBranch = [&](bool allowElseBranches) -> std::pair<StmtPtr, BlockTerminator>
        {
            std::vector<StmtPtr> stmts;
            BlockTerminator term = BlockTerminator::None;
            auto predicate = [&](int, il::support::SourceLoc) {
                if (parser.at(TokenKind::KeywordEnd) && parser.peek(1).kind == TokenKind::KeywordIf)
                    return true;
                if (!allowElseBranches)
                    return false;
                if (parser.at(TokenKind::KeywordElseIf))
                    return true;
                if (parser.at(TokenKind::KeywordElse))
                    return true;
                return false;
            };
            auto consumer = [&](int lineNumber,
                                 il::support::SourceLoc,
                                 StatementSequencer::TerminatorInfo &info) {
                info.line = lineNumber;
                info.loc = parser.peek().loc;
                if (parser.at(TokenKind::KeywordEnd) && parser.peek(1).kind == TokenKind::KeywordIf)
                {
                    Token endTok = parser.consume();
                    info.loc = endTok.loc;
                    parser.expect(TokenKind::KeywordIf);
                    term = BlockTerminator::EndIf;
                    return;
                }
                if (!allowElseBranches)
                    return;
                if (parser.at(TokenKind::KeywordElseIf))
                {
                    term = BlockTerminator::ElseIf;
                    return;
                }
                if (parser.at(TokenKind::KeywordElse))
                {
                    if (parser.peek(1).kind == TokenKind::KeywordIf)
                    {
                        term = BlockTerminator::ElseIf;
                    }
                    else
                    {
                        term = BlockTerminator::Else;
                    }
                }
            };
            driver.sequencer().collectStatements(predicate, consumer, stmts);
            return {makeBranchBody(std::move(stmts)), term};
        };

        auto [thenBranch, term] = collectBranch(true);
        stmt->then_branch = std::move(thenBranch);

        std::vector<IfStmt::ElseIf> elseifs;
        StmtPtr elseStmt;
        while (term == BlockTerminator::ElseIf)
        {
            IfStmt::ElseIf ei;
            if (parser.at(TokenKind::KeywordElseIf))
            {
                parser.consume();
            }
            else if (parser.at(TokenKind::KeywordElse))
            {
                parser.consume();
                parser.expect(TokenKind::KeywordIf);
            }
            else
            {
                break;
            }
            ei.cond = parser.parseExpression();
            parser.expect(TokenKind::KeywordThen);
            auto [branchBody, nextTerm] = collectBranch(true);
            ei.then_branch = std::move(branchBody);
            elseifs.push_back(std::move(ei));
            term = nextTerm;
        }

        if (term == BlockTerminator::Else)
        {
            parser.consume();
            auto [elseBody, endTerm] = collectBranch(false);
            elseStmt = std::move(elseBody);
            term = endTerm;
        }

        if (term != BlockTerminator::EndIf)
        {
            if (parser.emitter_)
            {
                parser.emitter_->emit(il::support::Severity::Error,
                                       "B0004",
                                       stmt->loc,
                                       2,
                                       "missing END IF");
            }
            else
            {
                std::fprintf(stderr, "missing END IF\n");
            }
            parser.syncToStmtBoundary();
        }

        stmt->elseifs = std::move(elseifs);
        stmt->else_branch = std::move(elseStmt);
    }
    else
    {
        StatementParseDriver driver(parser, line);
        auto thenStmt = driver.parseBranchBody();
        std::vector<IfStmt::ElseIf> elseifs;
        StmtPtr elseStmt;
        while (true)
        {
            driver.skipOptionalLineLabel({TokenKind::KeywordElseIf, TokenKind::KeywordElse});
            if (parser.at(TokenKind::KeywordElseIf))
            {
                parser.consume();
                IfStmt::ElseIf ei;
                ei.cond = parser.parseExpression();
                parser.expect(TokenKind::KeywordThen);
                ei.then_branch = driver.parseBranchBody();
                elseifs.push_back(std::move(ei));
                continue;
            }
            if (parser.at(TokenKind::KeywordElse))
            {
                parser.consume();
                if (parser.at(TokenKind::KeywordIf))
                {
                    parser.consume();
                    IfStmt::ElseIf ei;
                    ei.cond = parser.parseExpression();
                    parser.expect(TokenKind::KeywordThen);
                    ei.then_branch = driver.parseBranchBody();
                    elseifs.push_back(std::move(ei));
                    continue;
                }
                else
                {
                    elseStmt = driver.parseBranchBody();
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

StmtPtr parseWhile(Parser &parser)
{
    auto loc = parser.peek().loc;
    parser.consume();
    auto cond = parser.parseExpression();
    auto stmt = std::make_unique<WhileStmt>();
    stmt->loc = loc;
    stmt->cond = std::move(cond);
    StatementParseDriver driver(parser, 0);
    driver.sequencer().collectStatements(TokenKind::KeywordWend, stmt->body);
    return stmt;
}

StmtPtr parseDo(Parser &parser)
{
    auto loc = parser.peek().loc;
    parser.consume();
    auto stmt = std::make_unique<DoStmt>();
    stmt->loc = loc;

    bool hasPreTest = false;
    if (parser.at(TokenKind::KeywordWhile) || parser.at(TokenKind::KeywordUntil))
    {
        hasPreTest = true;
        Token testTok = parser.consume();
        stmt->testPos = DoStmt::TestPos::Pre;
        stmt->condKind =
            testTok.kind == TokenKind::KeywordWhile ? DoStmt::CondKind::While : DoStmt::CondKind::Until;
        stmt->cond = parser.parseExpression();
    }

    StatementParseDriver driver(parser, 0);
    driver.sequencer().collectStatements(TokenKind::KeywordLoop, stmt->body);

    bool hasPostTest = false;
    Token postTok{};
    DoStmt::CondKind postKind = DoStmt::CondKind::None;
    ExprPtr postCond;
    if (parser.at(TokenKind::KeywordWhile) || parser.at(TokenKind::KeywordUntil))
    {
        postTok = parser.consume();
        hasPostTest = true;
        postKind = postTok.kind == TokenKind::KeywordWhile ? DoStmt::CondKind::While
                                                            : DoStmt::CondKind::Until;
        postCond = parser.parseExpression();
    }

    if (hasPreTest && hasPostTest)
    {
        if (parser.emitter_)
        {
            parser.emitter_->emit(il::support::Severity::Error,
                                  "B0001",
                                  postTok.loc,
                                  static_cast<uint32_t>(postTok.lexeme.size()),
                                  "multiple DO loop tests");
        }
        else
        {
            std::fprintf(stderr, "multiple DO loop tests\n");
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

StmtPtr parseFor(Parser &parser)
{
    auto loc = parser.peek().loc;
    parser.consume();
    std::string var = parser.peek().lexeme;
    parser.expect(TokenKind::Identifier);
    parser.expect(TokenKind::Equal);
    auto start = parser.parseExpression();
    parser.expect(TokenKind::KeywordTo);
    auto end = parser.parseExpression();
    ExprPtr step;
    if (parser.at(TokenKind::KeywordStep))
    {
        parser.consume();
        step = parser.parseExpression();
    }
    auto stmt = std::make_unique<ForStmt>();
    stmt->loc = loc;
    stmt->var = var;
    stmt->start = std::move(start);
    stmt->end = std::move(end);
    stmt->step = std::move(step);
    StatementParseDriver driver(parser, 0);
    driver.sequencer().collectStatements(
        [&](int, il::support::SourceLoc) { return parser.at(TokenKind::KeywordNext); },
        [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &) {
            parser.consume();
            if (parser.at(TokenKind::Identifier))
                parser.consume();
        },
        stmt->body);
    return stmt;
}

std::pair<std::vector<StmtPtr>, il::support::SourceLoc> parseCaseElseBody(Parser &parser)
{
    parser.expect(TokenKind::KeywordCase);
    parser.expect(TokenKind::KeywordElse);
    Token elseEol = parser.expect(TokenKind::EndOfLine);

    std::pair<std::vector<StmtPtr>, il::support::SourceLoc> result;
    StatementParseDriver driver(parser, 0);
    auto predicate = [&](int, il::support::SourceLoc) {
        if (parser.at(TokenKind::KeywordCase))
            return true;
        if (parser.at(TokenKind::KeywordEnd) && parser.peek(1).kind == TokenKind::KeywordSelect)
            return true;
        return false;
    };
    auto consumer = [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &) {
    };
    driver.sequencer().collectStatements(predicate, consumer, result.first);
    result.second = elseEol.loc;
    return result;
}

CaseArm parseCaseArm(Parser &parser)
{
    Token caseTok = parser.expect(TokenKind::KeywordCase);
    CaseArm arm;
    arm.range.begin = caseTok.loc;

    bool haveEntry = false;
    while (true)
    {
        if (parser.at(TokenKind::Identifier) && parser.peek().lexeme == "IS")
        {
            parser.consume();
            CaseArm::CaseRel rel;
            Token opTok = parser.peek();
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
                        if (parser.emitter_)
                        {
                            parser.emitter_->emit(il::support::Severity::Error,
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
            parser.consume();

            int sign = 1;
            if (parser.at(TokenKind::Plus) || parser.at(TokenKind::Minus))
            {
                sign = parser.at(TokenKind::Minus) ? -1 : 1;
                parser.consume();
            }

            if (!parser.at(TokenKind::Number))
            {
                Token bad = parser.peek();
                if (bad.kind != TokenKind::EndOfLine)
                {
                    if (parser.emitter_)
                    {
                        parser.emitter_->emit(il::support::Severity::Error,
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

            Token valueTok = parser.consume();
            long long value = std::strtoll(valueTok.lexeme.c_str(), nullptr, 10);
            rel.rhs = static_cast<int64_t>(sign * value);
            arm.rels.push_back(rel);
            haveEntry = true;
        }
        else if (parser.at(TokenKind::String))
        {
            const Token stringTok = parser.peek();
            std::string decoded;
            std::string err;
            if (!il::io::decodeEscapedString(stringTok.lexeme, decoded, &err))
            {
                if (parser.emitter_)
                {
                    parser.emitter_->emit(il::support::Severity::Error,
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
            parser.consume();
            arm.str_labels.push_back(std::move(decoded));
            haveEntry = true;
        }
        else if (parser.at(TokenKind::Number))
        {
            Token labelTok = parser.consume();
            long long value = std::strtoll(labelTok.lexeme.c_str(), nullptr, 10);
            int64_t lo = static_cast<int64_t>(value);

            if (parser.at(TokenKind::KeywordTo))
            {
                parser.consume();
                if (!parser.at(TokenKind::Number))
                {
                    Token bad = parser.peek();
                    if (bad.kind != TokenKind::EndOfLine)
                    {
                        if (parser.emitter_)
                        {
                            parser.emitter_->emit(il::support::Severity::Error,
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

                Token hiTok = parser.consume();
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
            Token bad = parser.peek();
            if (bad.kind != TokenKind::EndOfLine)
            {
                if (parser.emitter_)
                {
                    parser.emitter_->emit(il::support::Severity::Error,
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

        if (parser.at(TokenKind::Comma))
        {
            parser.consume();
            continue;
        }
        break;
    }

exitCaseEntries:

    if (!haveEntry)
    {
        if (parser.emitter_)
        {
            parser.emitter_->emit(il::support::Severity::Error,
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

    Token caseEol = parser.expect(TokenKind::EndOfLine);
    arm.range.end = caseEol.loc;

    StatementParseDriver driver(parser, 0);
    auto predicate = [&](int, il::support::SourceLoc) {
        if (parser.at(TokenKind::KeywordCase))
            return true;
        if (parser.at(TokenKind::KeywordEnd) && parser.peek(1).kind == TokenKind::KeywordSelect)
            return true;
        return false;
    };
    auto consumer = [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &) {
    };
    driver.sequencer().collectStatements(predicate, consumer, arm.body);

    return arm;
}

StmtPtr parseSelectCase(Parser &parser)
{
    auto loc = parser.peek().loc;
    parser.consume();
    parser.expect(TokenKind::KeywordCase);
    auto selector = parser.parseExpression();
    Token headerEnd = parser.expect(TokenKind::EndOfLine);

    auto stmt = std::make_unique<SelectCaseStmt>();
    stmt->loc = loc;
    stmt->selector = std::move(selector);
    stmt->range.begin = loc;
    stmt->range.end = headerEnd.loc;

    auto diagnose = [&](il::support::SourceLoc diagLoc,
                        uint32_t length,
                        std::string_view message,
                        std::string_view code = "B0001")
    {
        if (parser.emitter_)
        {
            parser.emitter_->emit(il::support::Severity::Error,
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

    while (!parser.at(TokenKind::EndOfFile))
    {
        while (parser.at(TokenKind::EndOfLine))
            parser.consume();

        if (parser.at(TokenKind::EndOfFile))
            break;

        if (parser.at(TokenKind::Number))
        {
            TokenKind next = parser.peek(1).kind;
            if (next == TokenKind::KeywordCase ||
                (next == TokenKind::KeywordEnd && parser.peek(2).kind == TokenKind::KeywordSelect))
            {
                parser.consume();
            }
        }

        if (parser.at(TokenKind::KeywordEnd) && parser.peek(1).kind == TokenKind::KeywordSelect)
        {
            parser.consume();
            Token selectTok = parser.expect(TokenKind::KeywordSelect);
            stmt->range.end = selectTok.loc;
            if (!sawCaseArm)
            {
                diagnose(selectTok.loc,
                         static_cast<uint32_t>(selectTok.lexeme.size()),
                         "SELECT CASE requires at least one CASE arm");
            }
            expectEndSelect = false;
            break;
        }

        if (!parser.at(TokenKind::KeywordCase))
        {
            Token unexpected = parser.consume();
            diagnose(unexpected.loc,
                     static_cast<uint32_t>(unexpected.lexeme.size()),
                     "expected CASE or END SELECT in SELECT CASE");
            continue;
        }

        Token caseTok = parser.peek();
        if (parser.peek(1).kind == TokenKind::KeywordElse)
        {
            const Token elseTok = parser.peek(1);
            if (sawCaseElse)
            {
                diagnose(elseTok.loc,
                         static_cast<uint32_t>(elseTok.lexeme.size()),
                         diag::ERR_SelectCase_DuplicateElse.text,
                         diag::ERR_SelectCase_DuplicateElse.id);
            }
            if (!sawCaseArm)
            {
                diagnose(elseTok.loc,
                         static_cast<uint32_t>(elseTok.lexeme.size()),
                         "CASE ELSE requires a preceding CASE arm");
            }

            auto [elseBody, elseEnd] = parseCaseElseBody(parser);
            if (!sawCaseElse)
            {
                stmt->elseBody = std::move(elseBody);
                stmt->range.end = elseEnd;
            }
            sawCaseElse = true;
            continue;
        }

        if (sawCaseElse)
        {
            diagnose(caseTok.loc,
                     static_cast<uint32_t>(caseTok.lexeme.size()),
                     "CASE arms must precede CASE ELSE");
        }

        CaseArm arm = parseCaseArm(parser);
        stmt->arms.push_back(std::move(arm));
        if (!stmt->arms.empty())
        {
            stmt->range.end = stmt->arms.back().range.end;
        }
        sawCaseArm = true;
    }

    if (expectEndSelect)
    {
        diagnose(stmt->loc,
                 6,
                 diag::ERR_SelectCase_MissingEndSelect.text,
                 diag::ERR_SelectCase_MissingEndSelect.id);
    }

    return stmt;
}

} // namespace control_flow

} // namespace il::frontends::basic
