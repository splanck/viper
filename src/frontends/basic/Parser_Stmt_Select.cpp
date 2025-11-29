//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"
#include "frontends/basic/SelectModel.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/Options.hpp"
#include "frontends/basic/constfold/Dispatch.hpp"
#include "viper/il/IO.hpp"

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
    auto endResult =
        handleEndSelect(*state.stmt, state.sawCaseArm, state.expectEndSelect, state.diagnose);
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

    // Finalize the SELECT model and diagnose missing END SELECT in one place.
    auto finalizeSelectCase = [&](SelectParseState &st) -> void
    {
        SelectModelBuilder builder(st.diagnose);
        st.stmt->model = builder.build(*st.stmt);
        if (st.expectEndSelect)
        {
            st.diagnose(st.selectLoc,
                        static_cast<uint32_t>(6),
                        diag::ERR_SelectCase_MissingEndSelect.text,
                        diag::ERR_SelectCase_MissingEndSelect.id);
        }
    };

    finalizeSelectCase(state);
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
    auto predicate = [&](int, il::support::SourceLoc)
    {
        if (at(TokenKind::KeywordCase))
            return true;
        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordSelect)
            return true;
        return false;
    };
    auto consumer = [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &info)
    { info.loc = peek().loc; };
    result.terminator = bodyCtx.collectStatements(predicate, consumer, result.body);
    return result;
}

Parser::SelectInlineBodyResult Parser::collectInlineSelectBody()
{
    SelectInlineBodyResult result;
    auto inlineCtx = statementSequencer();

    while (!at(TokenKind::EndOfFile))
    {
        if (at(TokenKind::EndOfLine))
            break;

        if (at(TokenKind::KeywordCase) ||
            (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordSelect))
        {
            break;
        }

        int line = 0;
        inlineCtx.withOptionalLineNumber([&](int currentLine, il::support::SourceLoc)
                                         { line = currentLine; });

        if (!at(TokenKind::EndOfLine) && !at(TokenKind::Colon))
        {
            auto stmt = parseStatement(line);
            if (stmt)
            {
                stmt->line = line;
                result.body.push_back(std::move(stmt));
            }
        }

        if (at(TokenKind::Colon))
        {
            consume();
            continue;
        }

        break;
    }

    Token eolTok = expect(TokenKind::EndOfLine);
    result.terminator = eolTok;
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

    std::vector<StmtPtr> inlineBody;
    Token elseTerminator;
    if (at(TokenKind::Colon))
    {
        consume();
        auto inlineResult = collectInlineSelectBody();
        inlineBody = std::move(inlineResult.body);
        elseTerminator = inlineResult.terminator;
    }
    else
    {
        elseTerminator = expect(TokenKind::EndOfLine);
    }
    auto bodyResult = collectSelectBody();
    result.emittedDiagnostic = result.emittedDiagnostic || bodyResult.emittedDiagnostic;
    if (!sawCaseElse)
    {
        auto combinedBody = std::move(inlineBody);
        for (auto &stmtPtr : bodyResult.body)
        {
            combinedBody.push_back(std::move(stmtPtr));
        }
        stmt.elseBody = std::move(combinedBody);
        stmt.range.end = elseTerminator.loc;
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

struct Parser::Cursor
{
    struct Relation
    {
        CaseArm::CaseRel::Op op{CaseArm::CaseRel::Op::EQ};
        int sign = 1;
        Token valueTok;
    };

    Token caseTok;
    Token caseEol;
    bool hasInlineBody = false;
    std::vector<Token> stringLabels;
    std::vector<Token> numericLabels;
    std::vector<std::pair<Token, Token>> ranges;
    std::vector<Relation> relations;
};

struct Parser::CaseArmSyntax
{
    Cursor *cursor = nullptr;
};

il::support::Expected<Parser::CaseArmSyntax> Parser::parseCaseArmSyntax(Cursor &cursor)
{
    cursor.stringLabels.clear();
    cursor.numericLabels.clear();
    cursor.ranges.clear();
    cursor.relations.clear();
    cursor.hasInlineBody = false;

    cursor.caseTok = expect(TokenKind::KeywordCase);

    bool bail = false;
    while (!bail)
    {
        if ((at(TokenKind::Identifier) && peek().lexeme == "IS") || at(TokenKind::KeywordIs))
        {
            consume();
            Cursor::Relation rel;
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
                        emitError("B0001", opTok, "CASE IS requires a relational operator");
                    }
                    bail = true;
                    break;
                }
            }
            if (bail)
                break;

            consume();
            rel.sign = 1;
            if (at(TokenKind::Plus) || at(TokenKind::Minus))
            {
                rel.sign = at(TokenKind::Minus) ? -1 : 1;
                consume();
            }

            if (!at(TokenKind::Number))
            {
                Token bad = peek();
                if (bad.kind != TokenKind::EndOfLine)
                {
                    emitError("B0001", bad, "SELECT CASE labels must be integer literals");
                }
                bail = true;
                break;
            }

            rel.valueTok = consume();
            cursor.relations.push_back(std::move(rel));
        }
        else if (at(TokenKind::String))
        {
            cursor.stringLabels.push_back(consume());
        }
        else if (at(TokenKind::Identifier) && FrontendOptions::enableSelectCaseConstLabels())
        {
            // Support CONST identifiers and CHR$() for labels.
            std::string ident = peek().lexeme;
            // Remove type suffix for canonicalization (e.g., CHR$ -> CHR)
            std::string identBase = ident;
            if (!identBase.empty() && (identBase.back() == '$' || identBase.back() == '%' ||
                                        identBase.back() == '#' || identBase.back() == '!' ||
                                        identBase.back() == '&'))
            {
                identBase.pop_back();
            }
            std::string canon = CanonicalizeIdent(identBase);

            // Handle CHR / CHR$ builtin: fold to a string literal if possible.
            if (canon == "chr" && peek(1).kind == TokenKind::LParen)
            {
                // Parse the builtin expression and try to fold to a string literal.
                auto expr = parseExpression();
                if (expr)
                {
                    // Check if this is a BuiltinCallExpr for CHR/CHR$
                    if (auto *bce = as<BuiltinCallExpr>(*expr))
                    {
                        if (bce->builtin == BuiltinCallExpr::Builtin::Chr && !bce->args.empty())
                        {
                            // Try to fold CHR$(arg) to a string literal
                            if (auto folded = constfold::foldChrLiteral(*bce->args[0]))
                            {
                                if (auto *se = as<StringExpr>(*folded))
                                {
                                    Token t;
                                    t.kind = TokenKind::String;
                                    t.loc = cursor.caseTok.loc;
                                    // Store raw string value without quotes (matching lexer behavior)
                                    t.lexeme = se->value;
                                    cursor.stringLabels.push_back(std::move(t));
                                    continue;
                                }
                            }
                        }
                    }
                    emitError("B0001", cursor.caseTok, "SELECT CASE string label must be a literal or CHR$ constant");
                }
                continue;
            }

            // CONST integer/string lookup
            if (auto itS = knownConstStrs_.find(canon); itS != knownConstStrs_.end())
            {
                consume();
                Token t;
                t.kind = TokenKind::String;
                t.loc = peek().loc;
                // Store raw string value without quotes (matching lexer behavior)
                t.lexeme = itS->second;
                cursor.stringLabels.push_back(std::move(t));
                continue;
            }
            if (auto itI = knownConstInts_.find(canon); itI != knownConstInts_.end())
            {
                consume();
                // Support optional "TO <ident|number>" range after a CONST numeric.
                long long loVal = itI->second;
                if (at(TokenKind::KeywordTo))
                {
                    consume();
                    // Optional sign
                    int hiSign = 1;
                    if (at(TokenKind::Minus) || at(TokenKind::Plus))
                    {
                        hiSign = at(TokenKind::Minus) ? -1 : 1;
                        consume();
                    }
                    long long hiVal = 0;
                    if (at(TokenKind::Number))
                    {
                        hiVal = std::strtoll(peek().lexeme.c_str(), nullptr, 10);
                        consume();
                    }
                    else if (at(TokenKind::Identifier))
                    {
                        std::string hiName = CanonicalizeIdent(peek().lexeme);
                        if (auto itHi = knownConstInts_.find(hiName); itHi != knownConstInts_.end())
                        {
                            hiVal = itHi->second;
                            consume();
                        }
                        else
                        {
                            emitError("B0001", peek(), "SELECT CASE range end must be an integer literal or CONST");
                            bail = true;
                            break;
                        }
                    }
                    else
                    {
                        emitError("B0001", peek(), "SELECT CASE range end must be an integer literal or CONST");
                        bail = true;
                        break;
                    }

                    Token loTok;
                    loTok.kind = TokenKind::Number;
                    loTok.loc = cursor.caseTok.loc;
                    loTok.lexeme = std::to_string(loVal);
                    Token hiTok;
                    hiTok.kind = TokenKind::Number;
                    hiTok.loc = cursor.caseTok.loc;
                    hiTok.lexeme = std::to_string(hiSign < 0 ? -hiVal : hiVal);
                    cursor.ranges.emplace_back(std::move(loTok), std::move(hiTok));
                }
                else
                {
                    Token t;
                    t.kind = TokenKind::Number;
                    t.loc = cursor.caseTok.loc;
                    t.lexeme = std::to_string(loVal);
                    cursor.numericLabels.push_back(std::move(t));
                }
                continue;
            }

            // Unknown identifier in CASE label
            Token bad = peek();
            if (bad.kind != TokenKind::EndOfLine)
            {
                emitError("B0001", bad, "SELECT CASE labels must be literals or CONSTs");
            }
            break;
        }
        else if (at(TokenKind::Number) || at(TokenKind::Minus) || at(TokenKind::Plus))
        {
            // Handle optional unary sign before number
            int sign = 1;
            if (at(TokenKind::Minus) || at(TokenKind::Plus))
            {
                sign = at(TokenKind::Minus) ? -1 : 1;
                consume();
            }

            if (!at(TokenKind::Number))
            {
                Token bad = peek();
                if (bad.kind != TokenKind::EndOfLine)
                {
                    emitError("B0001", bad, "SELECT CASE labels must be integer literals");
                }
                bail = true;
                break;
            }

            Token loTok = consume();

            // Apply sign to the token's value for later processing
            if (sign == -1)
            {
                // Prepend minus to lexeme for correct parsing in lowerCaseArm
                loTok.lexeme = "-" + loTok.lexeme;
            }

            if (at(TokenKind::KeywordTo))
            {
                consume();

                // Handle optional sign for high end of range
                int hiSign = 1;
                if (at(TokenKind::Minus) || at(TokenKind::Plus))
                {
                    hiSign = at(TokenKind::Minus) ? -1 : 1;
                    consume();
                }

                if (!at(TokenKind::Number))
                {
                    Token bad = peek();
                    if (bad.kind != TokenKind::EndOfLine)
                    {
                        emitError("B0001", bad, "SELECT CASE labels must be integer literals");
                    }
                    bail = true;
                    break;
                }

                Token hiTok = consume();
                if (hiSign == -1)
                {
                    hiTok.lexeme = "-" + hiTok.lexeme;
                }
                cursor.ranges.emplace_back(std::move(loTok), std::move(hiTok));
            }
            else
            {
                cursor.numericLabels.push_back(std::move(loTok));
            }
        }
        else
        {
            Token bad = peek();
            if (bad.kind != TokenKind::EndOfLine)
            {
                emitError("B0001", bad, "SELECT CASE labels must be integer literals");
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

    if (at(TokenKind::Colon))
    {
        cursor.caseEol = peek();
        cursor.hasInlineBody = true;
        consume();
    }
    else
    {
        cursor.caseEol = expect(TokenKind::EndOfLine);
    }

    CaseArmSyntax syntax;
    syntax.cursor = &cursor;
    return syntax;
}

il::support::Expected<CaseArm> Parser::lowerCaseArm(const CaseArmSyntax &syntax)
{
    const Cursor &cursor = *syntax.cursor;

    CaseArm arm;
    arm.range.begin = cursor.caseTok.loc;
    arm.range.end = cursor.caseEol.loc;
    arm.caseKeywordLength = static_cast<uint32_t>(cursor.caseTok.lexeme.size());

    for (const Token &labelTok : cursor.numericLabels)
    {
        long long value = std::strtoll(labelTok.lexeme.c_str(), nullptr, 10);
        arm.labels.push_back(static_cast<int64_t>(value));
    }

    for (const auto &rangeTok : cursor.ranges)
    {
        long long lo = std::strtoll(rangeTok.first.lexeme.c_str(), nullptr, 10);
        long long hi = std::strtoll(rangeTok.second.lexeme.c_str(), nullptr, 10);
        arm.ranges.emplace_back(static_cast<int64_t>(lo), static_cast<int64_t>(hi));
    }

    for (const Cursor::Relation &relTok : cursor.relations)
    {
        long long value = std::strtoll(relTok.valueTok.lexeme.c_str(), nullptr, 10);
        CaseArm::CaseRel rel;
        rel.op = relTok.op;
        rel.rhs = static_cast<int64_t>(relTok.sign * value);
        arm.rels.push_back(rel);
    }

    for (const Token &stringTok : cursor.stringLabels)
    {
        std::string decoded;
        std::string err;
        if (!il::io::decodeEscapedString(stringTok.lexeme, decoded, &err))
        {
            emitError("B0003", stringTok, err);
            decoded = stringTok.lexeme;
        }
        arm.str_labels.push_back(std::move(decoded));
    }

    return arm;
}

Parser::ErrorOr<void> Parser::validateCaseArm(const CaseArm &arm)
{
    if (!arm.labels.empty() || !arm.str_labels.empty() || !arm.ranges.empty() || !arm.rels.empty())
    {
        return ErrorOr<void>{};
    }

    if (emitter_)
    {
        emitter_->emit(il::support::Severity::Error,
                       std::string(diag::ERR_Case_EmptyLabelList.id),
                       arm.range.begin,
                       arm.caseKeywordLength,
                       std::string(diag::ERR_Case_EmptyLabelList.text));
    }
    else
    {
        const std::string msg(diag::ERR_Case_EmptyLabelList.text);
        std::fprintf(stderr, "%s\n", msg.c_str());
    }

    return ErrorOr<void>{};
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
    Cursor cursor;
    auto syntax = parseCaseArmSyntax(cursor);
    if (!syntax)
    {
        syncToStmtBoundary();
        return {};
    }

    std::vector<StmtPtr> inlineBody;
    if (cursor.hasInlineBody)
    {
        auto inlineResult = collectInlineSelectBody();
        inlineBody = std::move(inlineResult.body);
        cursor.caseEol = inlineResult.terminator;
    }

    auto lowered = lowerCaseArm(syntax.value());
    if (!lowered)
    {
        syncToStmtBoundary();
        return {};
    }

    CaseArm arm = std::move(lowered.value());
    (void)validateCaseArm(arm);

    auto bodyResult = collectSelectBody();
    if (!inlineBody.empty())
    {
        for (auto &stmt : bodyResult.body)
        {
            inlineBody.push_back(std::move(stmt));
        }
        arm.body = std::move(inlineBody);
    }
    else
    {
        arm.body = std::move(bodyResult.body);
    }

    return arm;
}

} // namespace il::frontends::basic
