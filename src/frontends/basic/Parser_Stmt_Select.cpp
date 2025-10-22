//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Parser_Stmt_Select.cpp
// Purpose: Parse BASIC SELECT CASE statements and build structured AST nodes.
// Key invariants: CASE arms and CASE ELSE blocks are validated for correct
//                 ordering while tracking selector ranges for diagnostics.
// Ownership/Lifetime: AST nodes are produced as `std::unique_ptr` values that
//                     transfer ownership to the caller.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
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

/// @brief Parse the `SELECT CASE` header and initialise parser state.
///
/// @details Consumes the `SELECT CASE` keywords, parses the selector expression,
///          records the source range, and prepares a diagnostic callback that
///          forwards errors to the configured emitter.  The partially constructed
///          @ref SelectCaseStmt is stored in the returned state.
///
/// @return Parse state capturing selector information and diagnostic plumbing.
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

/// @brief Attempt to parse a `CASE ELSE` block for the current SELECT statement.
///
/// @details Delegates to @ref consumeCaseElse, updating parser bookkeeping about
///          whether an `ELSE` arm has been seen.  The helper returns whether the
///          directive was consumed so callers can adjust their parsing loop.
///
/// @param state Current SELECT parse state.
/// @return True when a `CASE ELSE` clause was handled.
bool Parser::parseSelectElse(SelectParseState &state)
{
    auto result = consumeCaseElse(*state.stmt, state.sawCaseArm, state.sawCaseElse, state.diagnose);
    return result.handled;
}

/// @brief Handle directives that may terminate or continue SELECT parsing.
///
/// @details Checks for `END SELECT` and `CASE ELSE` directives before the parser
///          attempts to parse a normal `CASE` arm.  The return value informs the
///          caller whether parsing should terminate, skip to the next iteration,
///          or continue with arm parsing.
///
/// @param state Current SELECT parse state.
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

/// @brief Parse all CASE arms within a SELECT block.
///
/// @details Iterates until `END SELECT` or end-of-file, dispatching directives
///          and parsing each `CASE` arm encountered.  Diagnostic hooks are used
///          to report unexpected tokens or missing terminators.
///
/// @param state Current SELECT parse state being populated.
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

/// @brief Parse an entire `SELECT CASE` statement.
///
/// @details Invokes @ref parseSelectHeader, parses all arms, and emits
///          diagnostics when an `END SELECT` keyword is missing.  The populated
///          statement is then returned for lowering.
///
/// @return Completed @ref SelectCaseStmt node.
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

/// @brief Collect the statements that form a CASE arm body.
///
/// @details Uses the statement sequencer to gather statements until another
///          `CASE` or `END SELECT` keyword is encountered.  The function records
///          the terminator token for diagnostics.
///
/// @return Body statements and metadata describing the terminator.
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

/// @brief Handle the `END SELECT` directive when encountered.
///
/// @details Validates that at least one `CASE` arm was present, updates the
///          statement's source range, and clears the expectation that an `END`
///          still needs to appear.
///
/// @param stmt Statement being populated.
/// @param sawCaseArm Whether any CASE arms were parsed previously.
/// @param expectEndSelect Flag that tracks whether `END SELECT` remains required.
/// @param diagnose Diagnostic callback for reporting errors.
/// @return Result structure indicating whether the directive was handled.
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

/// @brief Parse a `CASE ELSE` clause if present.
///
/// @details Verifies that the clause is not duplicated and that at least one
///          `CASE` arm preceded it.  The function collects the clause's body and
///          stores it on the statement when appropriate.
///
/// @param stmt Statement being populated.
/// @param sawCaseArm Whether any CASE arms have been parsed.
/// @param sawCaseElse Tracks whether a CASE ELSE was already encountered.
/// @param diagnose Diagnostic callback.
/// @return Result structure describing whether parsing succeeded and if a
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

/// @brief Parse the statements belonging to a `CASE ELSE` arm.
///
/// @details Consumes the `CASE ELSE` keywords, captures the end-of-line location
///          for range tracking, and then gathers the body statements until the
///          next directive terminates the block.
///
/// @return Pair of body statements and the location of the terminating EOL.
std::pair<std::vector<StmtPtr>, il::support::SourceLoc> Parser::parseCaseElseBody()
{
    expect(TokenKind::KeywordCase);
    expect(TokenKind::KeywordElse);
    Token elseEol = expect(TokenKind::EndOfLine);

    auto bodyResult = collectSelectBody();
    auto body = std::move(bodyResult.body);

    return {std::move(body), elseEol.loc};
}

/// @brief Parse a single `CASE` arm, including labels and body.
///
/// @details Handles relational forms (`CASE IS`), string literals, numeric
///          literals, and ranges while emitting diagnostics for malformed
///          entries.  The function then collects the arm body statements using
///          @ref collectSelectBody and records the source range.
///
/// @return Parsed case arm with labels, ranges, and body statements populated.
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

