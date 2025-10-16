// File: src/frontends/basic/Parser_Stmt_Select.cpp
// Purpose: Implements SELECT CASE parsing routines for the BASIC parser.
// Key invariants: Validates CASE and CASE ELSE structure while maintaining
//                 selector range bookkeeping.
// Ownership/Lifetime: Parser generates AST nodes owned by the caller via
//                     unique_ptr wrappers.
// License: MIT; see LICENSE for details.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "il/io/StringEscape.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

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

void Parser::emitCaseDiagnostic(const Token &tok,
                                std::string_view message,
                                std::string_view code)
{
    if (emitter_)
    {
        emitter_->emit(il::support::Severity::Error,
                       std::string(code),
                       tok.loc,
                       static_cast<uint32_t>(tok.lexeme.size()),
                       std::string(message));
    }
    else
    {
        std::fprintf(stderr,
                     "%.*s\n",
                     static_cast<int>(message.size()),
                     message.data());
    }
}

bool Parser::parseStringLabel(CaseArm &arm)
{
    const Token stringTok = peek();
    std::string decoded;
    std::string err;
    if (!il::io::decodeEscapedString(stringTok.lexeme, decoded, &err))
    {
        emitCaseDiagnostic(stringTok, err, "B0003");
        decoded = stringTok.lexeme;
    }
    consume();
    arm.str_labels.push_back(std::move(decoded));
    return true;
}

bool Parser::parseRangeLabel(CaseArm &arm, int64_t lowerBound)
{
    if (!at(TokenKind::Number))
    {
        Token bad = peek();
        if (bad.kind != TokenKind::EndOfLine)
        {
            emitCaseDiagnostic(bad,
                               "SELECT CASE labels must be integer literals",
                               "B0001");
        }
        return false;
    }

    Token hiTok = consume();
    long long hiValue = std::strtoll(hiTok.lexeme.c_str(), nullptr, 10);
    arm.ranges.emplace_back(lowerBound, static_cast<int64_t>(hiValue));
    return true;
}

bool Parser::parseNumericLabel(CaseArm &arm)
{
    Token labelTok = consume();
    long long value = std::strtoll(labelTok.lexeme.c_str(), nullptr, 10);
    int64_t lo = static_cast<int64_t>(value);

    if (at(TokenKind::KeywordTo))
    {
        consume();
        return parseRangeLabel(arm, lo);
    }

    arm.labels.push_back(lo);
    return true;
}

CaseArm Parser::parseCaseArm()
{
    Token caseTok = expect(TokenKind::KeywordCase);
    CaseArm arm;
    arm.range.begin = caseTok.loc;

    bool haveEntry = false;
    bool encounteredError = false;
    while (!encounteredError)
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
                        emitCaseDiagnostic(opTok,
                                           "CASE IS requires a relational operator",
                                           "B0001");
                    }
                    encounteredError = true;
                    break;
                }
            }
            if (encounteredError)
            {
                break;
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
                    emitCaseDiagnostic(bad,
                                       "SELECT CASE labels must be integer literals",
                                       "B0001");
                }
                encounteredError = true;
                break;
            }

            Token valueTok = consume();
            long long value = std::strtoll(valueTok.lexeme.c_str(), nullptr, 10);
            rel.rhs = static_cast<int64_t>(sign * value);
            arm.rels.push_back(rel);
            haveEntry = true;
        }
        else if (at(TokenKind::String))
        {
            if (parseStringLabel(arm))
            {
                haveEntry = true;
            }
        }
        else if (at(TokenKind::Number))
        {
            if (!parseNumericLabel(arm))
            {
                encounteredError = true;
            }
            else
            {
                haveEntry = true;
            }
        }
        else
        {
            Token bad = peek();
            if (bad.kind != TokenKind::EndOfLine)
            {
                emitCaseDiagnostic(bad,
                                   "SELECT CASE labels must be integer literals",
                                   "B0001");
            }
            break;
        }

        if (encounteredError)
        {
            break;
        }

        if (at(TokenKind::Comma))
        {
            consume();
            continue;
        }
        break;
    }

    if (!haveEntry)
    {
        emitCaseDiagnostic(caseTok,
                           std::string_view(diag::ERR_Case_EmptyLabelList.text),
                           std::string_view(diag::ERR_Case_EmptyLabelList.id));
    }

    Token caseEol = expect(TokenKind::EndOfLine);
    arm.range.end = caseEol.loc;

    auto bodyResult = collectSelectBody();
    arm.body = std::move(bodyResult.body);

    return arm;
}

} // namespace il::frontends::basic

