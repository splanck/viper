//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Parser_Stmt_Select.cpp
// Purpose: Implement SELECT CASE parsing routines for the BASIC front-end
//          parser.
// Key invariants: Maintains selector range bookkeeping while validating CASE
//                 and CASE ELSE structure.
// Ownership/Lifetime: Parser generates AST nodes owned by the caller via
//                     unique_ptr wrappers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parsing helpers for BASIC's SELECT CASE statement.
/// @details The functions in this file break down the SELECT CASE grammar into
///          focused helpers: parsing the header, dispatching directives like END
///          SELECT or CASE ELSE, and collecting individual CASE arms.  The
///          parser builds a `SelectCaseStmt` AST node while recording diagnostics
///          through the shared emitter when malformed constructs are encountered.

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

/// @brief Parse the SELECT CASE header and initialise tracking state.
/// @details Consumes the `SELECT CASE <expr>` header, materialises a
///          `SelectCaseStmt`, and captures a diagnostic lambda that reports
///          through the parser's emitter when available.  The returned state is
///          subsequently threaded through the rest of the parsing routine.
/// @return SelectParseState capturing the partially built statement and helpers.
Parser::SelectParseState Parser::parseSelectHeader()
{
    SelectParseState state;
    state.selectLoc = peek().loc;
    consume(); // SELECT
    expect(TokenKind::KeywordCase);
    auto selector = parseExpression();
    Token headerEnd = expect(TokenKind::EndOfLine);

    state.stmt = std::make_unique<SelectCaseStmt>();
    state.stmt->loc = state.selectLoc;
    state.stmt->selector = std::move(selector);
    state.stmt->range.begin = state.selectLoc;
    state.stmt->range.end = headerEnd.loc;

    state.diagnose = [&](il::support::SourceLoc diagLoc,
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
            return;
        }
        std::fprintf(stderr, "%.*s\n", static_cast<int>(message.size()), message.data());
    };

    return state;
}

/// @brief Attempt to parse a CASE ELSE directive for the active SELECT state.
/// @details Delegates to @ref consumeCaseElse and returns whether the directive
///          was handled, allowing the caller to continue scanning or exit early.
/// @param state SELECT parsing state that carries progress flags.
/// @return True if a CASE ELSE arm was consumed; otherwise false.
bool Parser::parseSelectElse(SelectParseState &state)
{
    auto result = consumeCaseElse(*state.stmt, state.sawCaseArm, state.sawCaseElse, state.diagnose);
    return result.handled;
}

/// @brief Dispatch directives encountered while scanning a SELECT body.
/// @details Handles END SELECT and CASE ELSE lines before normal arm parsing.
///          The returned action instructs the caller whether to terminate
///          parsing, continue scanning, or treat the line as a CASE arm.
/// @param state Mutable SELECT parsing state.
/// @return Dispatch action describing how the caller should proceed.
Parser::SelectDispatchAction Parser::dispatchSelectDirective(SelectParseState &state)
{
    auto endResult = handleEndSelect(*state.stmt, state.sawCaseArm, state.expectEndSelect, state.diagnose);
    if (endResult.handled)
        return SelectDispatchAction::Terminate;

    if (parseSelectElse(state))
        return SelectDispatchAction::Continue;

    return SelectDispatchAction::None;
}

/// @brief Parse all CASE arms belonging to the current SELECT statement.
/// @details Iterates through the token stream, skipping blank lines, handling
///          directives, and invoking @ref parseCaseArm for each CASE block.  As
///          arms are collected the encompassing statement range is updated.
/// @param state SELECT parsing state containing the partially built AST.
void Parser::parseSelectArms(SelectParseState &state)
{
    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::EndOfFile))
            return;

        if (at(TokenKind::Number))
        {
            TokenKind next = peek(1).kind;
            if (next == TokenKind::KeywordCase ||
                (next == TokenKind::KeywordEnd && peek(2).kind == TokenKind::KeywordSelect))
            {
                consume();
            }
        }

        const auto action = dispatchSelectDirective(state);
        if (action == SelectDispatchAction::Terminate)
            return;
        if (action == SelectDispatchAction::Continue)
            continue;

        if (!at(TokenKind::KeywordCase))
        {
            Token unexpected = consume();
            state.diagnose(unexpected.loc,
                           static_cast<uint32_t>(unexpected.lexeme.size()),
                           "expected CASE or END SELECT in SELECT CASE",
                           "B0001");
            continue;
        }

        Token caseTok = peek();
        CaseArm arm = parseCaseArm();
        arm.range.begin = caseTok.loc;
        state.stmt->arms.push_back(std::move(arm));
        if (!state.stmt->arms.empty())
        {
            state.stmt->range.end = state.stmt->arms.back().range.end;
        }
        state.sawCaseArm = true;
    }
}

/// @brief Parse an entire SELECT CASE statement and return its AST node.
/// @details Builds the header, collects arms, emits diagnostics for missing END
///          SELECT directives, and transfers ownership of the constructed
///          statement to the caller.
/// @return AST node representing the parsed SELECT CASE statement.
StmtPtr Parser::parseSelectCaseStatement()
{
    auto state = parseSelectHeader();
    parseSelectArms(state);

    if (state.expectEndSelect)
    {
        state.diagnose(state.selectLoc,
                       static_cast<uint32_t>(6),
                       diag::ERR_SelectCase_MissingEndSelect.text,
                       diag::ERR_SelectCase_MissingEndSelect.id);
    }

    return std::move(state.stmt);
}

/// @brief Collect the statements belonging to a single CASE arm or ELSE body.
/// @details Delegates to the statement sequencer to capture statements until the
///          next CASE/END SELECT terminator is seen.  Records the terminator
///          location so diagnostics can highlight the control-flow boundary.
/// @return Result containing the collected body and terminator metadata.
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

/// @brief Handle an END SELECT directive, emitting diagnostics as needed.
/// @details Confirms the upcoming tokens match `END SELECT`, finalises the
///          statement range, and reports missing CASE arms.  Updates the
///          `expectEndSelect` flag that tracks whether the terminator has been
///          seen.
/// @param stmt SELECT CASE statement being populated.
/// @param sawCaseArm Whether any CASE arms have been parsed so far.
/// @param expectEndSelect Flag indicating an END SELECT is still expected.
/// @param diagnose Diagnostic callback used to report errors.
/// @return Handler result describing whether the directive was consumed.
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

/// @brief Consume a CASE ELSE arm if present.
/// @details Validates structural rules (only one CASE ELSE, must follow at least
///          one CASE), captures the body statements, and updates tracking flags.
/// @param stmt Statement under construction.
/// @param sawCaseArm True when a CASE arm has already been parsed.
/// @param sawCaseElse Flag updated to record whether an ELSE arm exists.
/// @param diagnose Diagnostic emitter callback.
/// @return Handler result containing diagnostic information and handled state.
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

/// @brief Parse the body of a CASE ELSE directive without modifying state.
/// @details Used by legacy call sites that expect the body and source location as
///          a pair instead of updating the active parse state.
/// @return Collected statements and the terminating location.
std::pair<std::vector<StmtPtr>, il::support::SourceLoc> Parser::parseCaseElseBody()
{
    expect(TokenKind::KeywordCase);
    expect(TokenKind::KeywordElse);
    Token elseEol = expect(TokenKind::EndOfLine);

    auto bodyResult = collectSelectBody();
    auto body = std::move(bodyResult.body);

    return {std::move(body), elseEol.loc};
}

/// @brief Parse a CASE arm including relational, range, and literal entries.
/// @details Supports relational `CASE IS <op> value`, discrete values, ranges
///          using the `TO` keyword, and string labels.  Diagnostics surface when
///          malformed entries are encountered.  The collected body is appended to
///          the returned `CaseArm`.
/// @return Populated CASE arm ready to be appended to the SELECT statement.
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

