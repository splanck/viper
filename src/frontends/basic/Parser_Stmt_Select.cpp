//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements SELECT CASE parsing for the BASIC frontend.  The helpers interpret
// selector expressions, CASE arms, and CASE ELSE bodies while maintaining the
// bookkeeping required to emit diagnostics for overlapping or malformed ranges.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parsing helpers for BASIC SELECT CASE statements.
/// @details The routines collaborate with @ref StatementSequencer to assemble
///          per-arm statement lists, track terminators, and normalise the parsed
///          representation into @ref SelectCaseStmt AST nodes.

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

/// @brief Parse a SELECT CASE statement including all arms.
/// @details Consumes the SELECT CASE keywords, parses the controlling expression,
///          and iteratively gathers CASE and CASE ELSE arms by delegating to
///          helper routines.  Ensures the statement is terminated by END SELECT
///          before returning the fully populated AST node.
/// @return AST node describing the SELECT CASE construct.
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

/// @brief Collect the statements that form the body of the current CASE arm.
/// @details Uses a @ref StatementSequencer configured with a predicate that stops
///          when the parser encounters CASE, CASE ELSE, or END SELECT terminators.
///          The helper returns both the gathered statements and terminator
///          metadata so callers can determine which keyword triggered the exit.
/// @return Pair containing the collected statements and terminator information.
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

/// @brief Handle the END SELECT terminator for the surrounding statement.
/// @details When the parser encounters END SELECT this helper consumes the
///          keyword sequence, records the closing location, and emits diagnostics
///          if the statement lacked CASE arms.  The @p expectEndSelect flag is
///          cleared so outer loops know the statement has been closed.
/// @param stmt            SELECT CASE statement being populated.
/// @param sawCaseArm      Indicates whether any CASE arm was parsed.
/// @param expectEndSelect Flag that tracks whether an END SELECT is still
///                        expected; cleared when the terminator is handled.
/// @param diagnose        Callback used to emit diagnostics.
/// @return Result indicating whether the caller handled the terminator and if a
///         diagnostic was produced.
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

/// @brief Consume a CASE ELSE arm and attach it to the statement.
/// @details Validates that a prior CASE arm exists, checks for duplicate CASE
///          ELSE clauses, and collects the associated statements.  The helper
///          updates @p sawCaseElse to prevent additional CASE ELSE arms from
///          being accepted.
/// @param stmt        SELECT CASE statement being populated.
/// @param sawCaseArm  Flag noting whether any CASE arm was parsed.
/// @param sawCaseElse Flag tracking whether a CASE ELSE arm already appeared.
/// @param diagnose    Callback used to emit diagnostics.
/// @return Result describing whether parsing consumed a CASE ELSE and if a
///         diagnostic was emitted.
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

/// @brief Parse the statement list that follows a CASE ELSE header.
/// @details Consumes the CASE ELSE keywords, captures the newline terminator,
///          and delegates to @ref collectSelectBody to gather the subsequent
///          statements until END SELECT or another terminator appears.  Returns
///          both the collected body and the location of the CASE ELSE header for
///          later range tracking.
/// @return Pair of statements and the location of the CASE ELSE newline token.
std::pair<std::vector<StmtPtr>, il::support::SourceLoc> Parser::parseCaseElseBody()
{
    expect(TokenKind::KeywordCase);
    expect(TokenKind::KeywordElse);
    Token elseEol = expect(TokenKind::EndOfLine);

    auto bodyResult = collectSelectBody();
    auto body = std::move(bodyResult.body);

    return {std::move(body), elseEol.loc};
}

/// @brief Parse a single CASE arm header and its statement body.
/// @details After consuming the CASE keyword the helper parses selector elements
///          (individual values or ranges), records their source spans, and then
///          gathers the associated statement list via @ref collectSelectBody.
///          Terminator metadata is stored in the returned @ref CaseArm so the
///          caller can decide whether more arms follow.
/// @return Fully populated CASE arm including selector metadata and body.
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

} // namespace il::frontends::basic

