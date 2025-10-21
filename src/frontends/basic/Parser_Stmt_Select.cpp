//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements parsing support for BASIC's SELECT CASE statement.  The routines
// here collaborate with the StatementSequencer to gather case bodies, enforce
// well-formed CASE/CASE ELSE structure, and emit diagnostics when malformed
// constructs are encountered.  Keeping the implementation in this translation
// unit isolates the complex multi-branch grammar from the parser registration
// logic.
//
// Links: docs/codemap.md

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

/// @brief Parse a SELECT CASE statement and all of its arms.
///
/// @details Consumes the `SELECT CASE` header, records the selector expression,
///          and iteratively parses each CASE or CASE ELSE arm.  The helper uses
///          @ref StatementSequencer to collect statements until a terminating
///          keyword is seen, diagnosing malformed constructs and missing terminators
///          via the provided diagnostic emitter.  The resulting AST node captures
///          the full range of the statement for later source mapping.
///
/// @return Owned AST node describing the parsed SELECT CASE statement.
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

/// @brief Collect the statements for a single CASE arm body.
///
/// @details Delegates to the StatementSequencer to accumulate statements until
///          another CASE or END SELECT token is encountered.  The terminator and
///          gathered body are packaged into a @ref SelectBodyResult structure for
///          the caller to inspect.
///
/// @return Structure describing the collected body and the terminator that ended it.
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

/// @brief Handle an encountered `END SELECT` terminator.
///
/// @details Updates the enclosing statement's source range, diagnoses missing
///          CASE arms, and flips the @p expectEndSelect guard so callers know the
///          statement was properly closed.  Returns a structure that records
///          whether the terminator was consumed and if a diagnostic was emitted.
///
/// @param stmt The SELECT CASE statement under construction.
/// @param sawCaseArm Tracks whether any CASE arms were parsed before the terminator.
/// @param expectEndSelect Reference toggled to @c false once the terminator is handled.
/// @param diagnose Callback used to emit diagnostics when malformed constructs are detected.
/// @return Result indicating whether the terminator was processed and if an error occurred.
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

/// @brief Handle a CASE ELSE arm if one is present at the current cursor.
///
/// @details Consumes the CASE ELSE tokens, diagnoses structural errors such as
///          duplicates or CASE ELSE appearing before any CASE arm, and records
///          the body statements if this is the first CASE ELSE encountered.
///          The helper reports whether it consumed a terminator and whether any
///          diagnostics were emitted so the caller can adjust parser state.
///
/// @param stmt Statement under construction that owns the eventual CASE ELSE body.
/// @param sawCaseArm Indicates whether at least one CASE arm has been parsed.
/// @param sawCaseElse Tracks whether a CASE ELSE was previously seen.
/// @param diagnose Diagnostic callback supplied by the caller.
/// @return Result describing whether the branch was consumed and if errors occurred.
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

/// @brief Parse a CASE ELSE body using the shared helpers.
///
/// @details Consumes the CASE ELSE header tokens, collects the body statements,
///          and returns both the statement list and the source location of the
///          terminating end-of-line token so callers can update range metadata.
///
/// @return Pair consisting of the parsed body statements and the terminator location.
std::pair<std::vector<StmtPtr>, il::support::SourceLoc> Parser::parseCaseElseBody()
{
    expect(TokenKind::KeywordCase);
    expect(TokenKind::KeywordElse);
    Token elseEol = expect(TokenKind::EndOfLine);

    auto bodyResult = collectSelectBody();
    auto body = std::move(bodyResult.body);

    return {std::move(body), elseEol.loc};
}

/// @brief Parse a single CASE arm, including its label set and body.
///
/// @details Handles relational selectors (`CASE IS <op>`), discrete labels,
///          string literals, and numeric ranges, emitting diagnostics for
///          malformed label lists.  After collecting the label metadata the
///          helper gathers the associated body statements via
///          @ref collectSelectBody and returns a fully-populated @ref CaseArm.
///
/// @return CaseArm structure describing the parsed labels and statements.
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

