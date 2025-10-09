// File: src/frontends/basic/Parser_Stmt.cpp
// Purpose: Implements statement-level parsing routines for the BASIC parser.
// Key invariants: Relies on token buffer for lookahead.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// License: MIT; see LICENSE for details.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace il::frontends::basic
{

void Parser::noteProcedureName(std::string name)
{
    knownProcedures_.insert(std::move(name));
}

bool Parser::isKnownProcedureName(const std::string &name) const
{
    return knownProcedures_.find(name) != knownProcedures_.end();
}

void Parser::emitParseError(il::support::SourceLoc loc,
                            uint32_t length,
                            const char *message,
                            const char *code)
{
    if (emitter_)
    {
        emitter_->emit(il::support::Severity::Error, code, loc, length, message);
    }
    else
    {
        std::fprintf(stderr, "%s\n", message);
    }
}

/// @brief Parse a single statement based on the current token.
/// @param line Line number associated with the statement.
/// @return Parsed statement node or nullptr for unrecognized statements.
StmtPtr Parser::parseStatement(int line)
{
    const Token &tok = peek();
    std::string tokLexeme = tok.lexeme;
    auto tokLoc = tok.loc;
    if (tok.kind == TokenKind::Number)
    {
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           tok.loc,
                           static_cast<uint32_t>(tok.lexeme.size()),
                           "unexpected line number");
        }
        else
        {
            std::fprintf(stderr, "unexpected line number '%s'\n", tok.lexeme.c_str());
        }

        while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
        {
            consume();
        }
        return nullptr;
    }

    const auto kind = tok.kind;
    const auto index = static_cast<std::size_t>(kind);
    if (index < stmtHandlers_.size())
    {
        const auto &handler = stmtHandlers_[index];
        if (handler.no_arg)
            return (this->*(handler.no_arg))();
        if (handler.with_line)
            return (this->*(handler.with_line))(line);
    }
    if (tok.kind == TokenKind::Identifier && peek(1).kind == TokenKind::LParen)
    {
        std::string ident = tokLexeme;
        auto identLoc = tokLoc;
        auto expr = parseArrayOrVar();
        if (auto *callExpr = dynamic_cast<CallExpr *>(expr.get()))
        {
            auto stmt = std::make_unique<CallStmt>();
            stmt->loc = identLoc;
            stmt->call.reset(static_cast<CallExpr *>(expr.release()));
            return stmt;
        }
        if (emitter_)
        {
            std::string msg = std::string("unknown statement '") + ident + "'";
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           identLoc,
                           static_cast<uint32_t>(ident.size()),
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr, "unknown statement '%s'\n", ident.c_str());
        }
        while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
        {
            consume();
        }
        return nullptr;
    }
    if (tok.kind == TokenKind::Identifier && isKnownProcedureName(tokLexeme) &&
        peek(1).kind != TokenKind::LParen)
    {
        std::string ident = tokLexeme;
        auto nextTok = peek(1);
        auto diagLoc = nextTok.loc.isValid() ? nextTok.loc : tokLoc;
        uint32_t length = 1;
        if (emitter_)
        {
            std::string msg = "expected '(' after procedure name '" + ident + "'";
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           diagLoc,
                           length,
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr,
                         "expected '(' after procedure name '%s'\n",
                         ident.c_str());
        }
        while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
        {
            consume();
        }
        return nullptr;
    }
    if (emitter_)
    {
            std::string msg = std::string("unknown statement '") + tokLexeme + "'";
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           tokLoc,
                           static_cast<uint32_t>(tokLexeme.size()),
                           std::move(msg));
    }
    else
    {
        std::fprintf(stderr, "unknown statement '%s'\n", tok.lexeme.c_str());
    }

    while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
    {
        consume();
    }

    return nullptr;
}

/// @brief Check whether @p kind marks the beginning of a statement.
/// @param kind Token to examine.
/// @return True when a handler or structural keyword introduces a new statement.
bool Parser::isStatementStart(TokenKind kind) const
{
    switch (kind)
    {
        case TokenKind::KeywordAnd:
        case TokenKind::KeywordOr:
        case TokenKind::KeywordNot:
        case TokenKind::KeywordAndAlso:
        case TokenKind::KeywordOrElse:
            return false;
        case TokenKind::KeywordThen:
        case TokenKind::KeywordElse:
        case TokenKind::KeywordElseIf:
        case TokenKind::KeywordWend:
        case TokenKind::KeywordTo:
        case TokenKind::KeywordStep:
        case TokenKind::KeywordAs:
            return true;
        default:
            break;
    }
    const auto index = static_cast<std::size_t>(kind);
    if (index >= stmtHandlers_.size())
        return false;
    const auto &handler = stmtHandlers_[index];
    return handler.no_arg != nullptr || handler.with_line != nullptr;
}

/// @brief Parse a PRINT statement.
/// @return PrintStmt containing expressions and separators in order.
/// @note Stops parsing at end-of-line, colon, or when a new statement keyword is encountered.
StmtPtr Parser::parsePrint()
{
    auto loc = peek().loc;
    consume(); // PRINT
    if (at(TokenKind::Hash))
    {
        consume();
        auto stmt = std::make_unique<PrintChStmt>();
        stmt->loc = loc;
        stmt->channelExpr = parseExpression();
        stmt->trailingNewline = true;
        if (at(TokenKind::Comma))
        {
            consume();
            while (true)
            {
                if (at(TokenKind::EndOfLine) || at(TokenKind::EndOfFile) || at(TokenKind::Colon))
                    break;
                if (isStatementStart(peek().kind))
                    break;
                stmt->args.push_back(parseExpression());
                if (!at(TokenKind::Comma))
                    break;
                consume();
            }
        }
        return stmt;
    }
    auto stmt = std::make_unique<PrintStmt>();
    stmt->loc = loc;
    while (!at(TokenKind::EndOfLine) && !at(TokenKind::EndOfFile) && !at(TokenKind::Colon))
    {
        TokenKind k = peek().kind;
        if (isStatementStart(k))
            break;
        if (at(TokenKind::Comma))
        {
            consume();
            stmt->items.push_back(PrintItem{PrintItem::Kind::Comma, nullptr});
            continue;
        }
        if (at(TokenKind::Semicolon))
        {
            consume();
            stmt->items.push_back(PrintItem{PrintItem::Kind::Semicolon, nullptr});
            continue;
        }
        stmt->items.push_back(PrintItem{PrintItem::Kind::Expr, parseExpression()});
    }
    return stmt;
}

/// @brief Parse a LET assignment statement.
/// @return LetStmt with target and assigned expression.
/// @note Expects an '=' between target and expression.
StmtPtr Parser::parseLet()
{
    auto loc = peek().loc;
    consume(); // LET
    auto target = parsePrimary();
    expect(TokenKind::Equal);
    auto e = parseExpression();
    auto stmt = std::make_unique<LetStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    stmt->expr = std::move(e);
    return stmt;
}

/// @brief Skip any optional line number that appears after a newline.
/// @param ctx Statement sequencer providing newline skipping helpers.
/// @param followerKinds Optional whitelist of tokens that may follow the line label.
void Parser::skipOptionalLineLabelAfterBreak(StatementSequencer &ctx,
                                             std::initializer_list<TokenKind> followerKinds)
{
    if (!at(TokenKind::EndOfLine))
        return;

    ctx.skipLineBreaks();

    if (!at(TokenKind::Number))
        return;

    if (!followerKinds.size())
    {
        consume();
        return;
    }

    TokenKind next = peek(1).kind;
    for (auto follower : followerKinds)
    {
        if (next == follower)
        {
            consume();
            return;
        }
    }
}

/// @brief Parse the body of a single IF branch while preserving separators.
/// @param line Line number propagated to nested statements.
/// @param ctx Statement sequencer providing separator helpers.
/// @return Parsed statement representing the branch body.
StmtPtr Parser::parseIfBranchBody(int line, StatementSequencer &ctx)
{
    skipOptionalLineLabelAfterBreak(ctx);
    auto stmt = parseStatement(line);
    if (stmt)
        stmt->line = line;
    return stmt;
}

/// @brief Parse an IF/THEN[/ELSEIF/ELSE] statement.
/// @param line Line number propagated to nested statements.
/// @return IfStmt with condition and branch nodes.
/// @note THEN branch shares the same line number as the IF keyword.
StmtPtr Parser::parseIf(int line)
{
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

        auto ctx = statementSequencer();

        auto makeBranchBody = [&](std::vector<StmtPtr> &&stmts) -> StmtPtr
        {
            if (stmts.empty())
                return nullptr;
            auto list = std::make_unique<StmtList>();
            list->line = line;
            il::support::SourceLoc listLoc = loc;
            for (const auto &bodyStmt : stmts)
            {
                if (bodyStmt)
                {
                    listLoc = bodyStmt->loc;
                    break;
                }
            }
            list->loc = listLoc;
            list->stmts = std::move(stmts);
            return list;
        };

        auto collectBranch = [&](bool allowElseBranches)
            -> std::pair<StmtPtr, BlockTerminator>
        {
            std::vector<StmtPtr> stmts;
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
                                 StatementSequencer::TerminatorInfo &info)
            {
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
            ctx.collectStatements(predicate, consumer, stmts);
            return {makeBranchBody(std::move(stmts)), term};
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
        auto ctx = statementSequencer();
        auto thenStmt = parseIfBranchBody(line, ctx);
        std::vector<IfStmt::ElseIf> elseifs;
        StmtPtr elseStmt;
        while (true)
        {
            skipOptionalLineLabelAfterBreak(ctx, {TokenKind::KeywordElseIf, TokenKind::KeywordElse});
            if (at(TokenKind::KeywordElseIf))
            {
                consume();
                IfStmt::ElseIf ei;
                ei.cond = parseExpression();
                expect(TokenKind::KeywordThen);
                ei.then_branch = parseIfBranchBody(line, ctx);
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
                    ei.then_branch = parseIfBranchBody(line, ctx);
                    elseifs.push_back(std::move(ei));
                    continue;
                }
                else
                {
                    elseStmt = parseIfBranchBody(line, ctx);
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

/// @brief Parse a WHILE loop terminated by WEND.
/// @return WhileStmt with condition and body statements.
/// @note Consumes statements until a matching WEND token.
StmtPtr Parser::parseWhile()
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

void Parser::collectSelectCaseBody(std::vector<StmtPtr> &dst)
{
    auto bodyCtx = statementSequencer();
    auto predicate = [&](int, il::support::SourceLoc)
    {
        if (at(TokenKind::KeywordCase))
            return true;
        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordSelect)
            return true;
        return false;
    };
    auto consumer = [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
    {
    };
    bodyCtx.collectStatements(predicate, consumer, dst);
}

CaseArm Parser::parseCaseArm()
{
    CaseArm arm;
    Token caseTok = expect(TokenKind::KeywordCase);
    arm.range.begin = caseTok.loc;

    bool haveLabel = false;
    while (true)
    {
        if (!at(TokenKind::Number))
        {
            Token bad = peek();
            if (bad.kind != TokenKind::EndOfLine)
            {
                emitParseError(bad.loc,
                               static_cast<uint32_t>(bad.lexeme.size()),
                               "SELECT CASE labels must be integer literals");
            }
            break;
        }
        Token labelTok = consume();
        long long value = std::strtoll(labelTok.lexeme.c_str(), nullptr, 10);
        arm.labels.push_back(static_cast<int64_t>(value));
        haveLabel = true;
        if (at(TokenKind::Comma))
        {
            consume();
            continue;
        }
        break;
    }

    if (!haveLabel)
    {
        emitParseError(caseTok.loc,
                       static_cast<uint32_t>(caseTok.lexeme.size()),
                       "CASE arm requires at least one label");
    }

    Token caseEol = expect(TokenKind::EndOfLine);
    arm.range.end = caseEol.loc;
    collectSelectCaseBody(arm.body);
    return arm;
}

/// @brief Parse a SELECT CASE statement terminating with END SELECT.
/// @return SelectCaseStmt containing selector, CASE arms, and optional CASE ELSE.
StmtPtr Parser::parseSelectCase()
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

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordSelect)
        {
            consume();
            Token selectTok = expect(TokenKind::KeywordSelect);
            stmt->range.end = selectTok.loc;
            if (!sawCaseArm)
            {
                emitParseError(selectTok.loc,
                               static_cast<uint32_t>(selectTok.lexeme.size()),
                               "SELECT CASE requires at least one CASE arm");
            }
            expectEndSelect = false;
            break;
        }

        if (!at(TokenKind::KeywordCase))
        {
            Token unexpected = consume();
            emitParseError(unexpected.loc,
                           static_cast<uint32_t>(unexpected.lexeme.size()),
                           "expected CASE or END SELECT in SELECT CASE");
            continue;
        }

        const Token &caseTok = peek();
        il::support::SourceLoc caseLoc = caseTok.loc;
        TokenKind nextKind = peek(1).kind;

        if (nextKind == TokenKind::KeywordElse)
        {
            consume(); // CASE
            Token elseTok = consume();
            if (sawCaseElse)
            {
                emitParseError(elseTok.loc,
                               static_cast<uint32_t>(elseTok.lexeme.size()),
                               "duplicate CASE ELSE arm");
            }
            if (!sawCaseArm)
            {
                emitParseError(elseTok.loc,
                               static_cast<uint32_t>(elseTok.lexeme.size()),
                               "CASE ELSE requires a preceding CASE arm");
            }
            sawCaseElse = true;
            Token elseEol = expect(TokenKind::EndOfLine);
            collectSelectCaseBody(stmt->elseBody);
            stmt->range.end = elseEol.loc;
            continue;
        }

        if (sawCaseElse)
        {
            emitParseError(caseLoc,
                           static_cast<uint32_t>(caseTok.lexeme.size()),
                           "CASE arms must precede CASE ELSE");
        }

        CaseArm arm = parseCaseArm();
        stmt->arms.push_back(std::move(arm));
        sawCaseArm = true;
    }

    if (expectEndSelect)
    {
        emitParseError(stmt->loc, 6, "missing END SELECT", "B0004");
    }

    return stmt;
}

/// @brief Parse a DO ... LOOP statement with optional pre/post conditions.
/// @return DoStmt capturing condition position and body statements.
StmtPtr Parser::parseDo()
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
        postTok = consume();
        hasPostTest = true;
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

/// @brief Parse a FOR loop terminated by NEXT.
/// @return ForStmt describing loop variable, bounds, and body.
/// @note Optional STEP expression is supported.
StmtPtr Parser::parseFor()
{
    auto loc = peek().loc;
    consume(); // FOR
    std::string var = peek().lexeme;
    expect(TokenKind::Identifier);
    expect(TokenKind::Equal);
    auto start = parseExpression();
    expect(TokenKind::KeywordTo);
    auto end = parseExpression();
    ExprPtr step;
    if (at(TokenKind::KeywordStep))
    {
        consume();
        step = parseExpression();
    }
    auto stmt = std::make_unique<ForStmt>();
    stmt->loc = loc;
    stmt->var = var;
    stmt->start = std::move(start);
    stmt->end = std::move(end);
    stmt->step = std::move(step);
    auto ctxFor = statementSequencer();
    ctxFor.collectStatements([&](int, il::support::SourceLoc) { return at(TokenKind::KeywordNext); },
                             [&](int,
                                 il::support::SourceLoc,
                                 StatementSequencer::TerminatorInfo &)
                             {
                                 consume();
                                 if (at(TokenKind::Identifier))
                                     consume();
                             },
                             stmt->body);
    return stmt;
}

/// @brief Parse a NEXT statement advancing a FOR loop.
/// @return NextStmt referencing an optional loop variable.
StmtPtr Parser::parseNext()
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
    stmt->var = name;
    return stmt;
}

/// @brief Parse an EXIT statement identifying the enclosing loop kind.
/// @return ExitStmt targeting FOR, WHILE, or DO loops.
StmtPtr Parser::parseExit()
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
        uint32_t length = unexpected.lexeme.empty()
                               ? 1u
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

/// @brief Parse a GOTO statement targeting a numeric line.
/// @return GotoStmt with destination line number.
StmtPtr Parser::parseGoto()
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

/// @brief Parse a GOSUB statement invoking a numbered subroutine.
/// @return GosubStmt describing the target line.
StmtPtr Parser::parseGosub()
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

/// @brief Parse an OPEN statement configuring file I/O channels.
/// @return OpenStmt capturing path, mode, and channel expressions.
StmtPtr Parser::parseOpen()
{
    auto loc = peek().loc;
    consume(); // OPEN
    auto stmt = std::make_unique<OpenStmt>();
    stmt->loc = loc;
    stmt->pathExpr = parseExpression();
    expect(TokenKind::KeywordFor);
    if (at(TokenKind::KeywordInput))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Input;
    }
    else if (at(TokenKind::KeywordOutput))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Output;
    }
    else if (at(TokenKind::KeywordAppend))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Append;
    }
    else if (at(TokenKind::KeywordBinary))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Binary;
    }
    else if (at(TokenKind::KeywordRandom))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Random;
    }
    else
    {
        Token unexpected = consume();
        if (emitter_)
        {
            emitter_->emitExpected(unexpected.kind, TokenKind::KeywordInput, unexpected.loc);
        }
    }
    expect(TokenKind::KeywordAs);
    expect(TokenKind::Hash);
    stmt->channelExpr = parseExpression();
    return stmt;
}

/// @brief Parse a CLOSE statement releasing a channel.
/// @return CloseStmt describing the channel expression.
StmtPtr Parser::parseClose()
{
    auto loc = peek().loc;
    consume(); // CLOSE
    auto stmt = std::make_unique<CloseStmt>();
    stmt->loc = loc;
    expect(TokenKind::Hash);
    stmt->channelExpr = parseExpression();
    return stmt;
}

/// @brief Parse an ON ERROR GOTO statement that configures error handling.
/// @return OnErrorGoto node describing the handler target or reset.
StmtPtr Parser::parseOnErrorGoto()
{
    auto loc = peek().loc;
    consume(); // ON
    expect(TokenKind::KeywordError);
    expect(TokenKind::KeywordGoto);
    Token targetTok = peek();
    int target = std::atoi(targetTok.lexeme.c_str());
    expect(TokenKind::Number);
    auto stmt = std::make_unique<OnErrorGoto>();
    stmt->loc = loc;
    stmt->target = target;
    stmt->toZero = targetTok.kind == TokenKind::Number && target == 0;
    return stmt;
}

/// @brief Parse an END statement.
/// @return EndStmt marking program or procedure termination.
StmtPtr Parser::parseEnd()
{
    auto loc = peek().loc;
    consume(); // END
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = loc;
    return stmt;
}

/// @brief Parse an INPUT statement.
/// @return InputStmt with optional prompt and variable target.
/// @note Parses a leading string literal prompt followed by a comma if present.
StmtPtr Parser::parseInput()
{
    auto loc = peek().loc;
    consume(); // INPUT
    ExprPtr prompt;
    if (at(TokenKind::String))
    {
        auto s = std::make_unique<StringExpr>();
        s->loc = peek().loc;
        s->value = peek().lexeme;
        prompt = std::move(s);
        consume();
        expect(TokenKind::Comma);
    }
    auto stmt = std::make_unique<InputStmt>();
    stmt->loc = loc;
    stmt->prompt = std::move(prompt);

    Token nameTok = expect(TokenKind::Identifier);
    stmt->vars.push_back(nameTok.lexeme);

    while (at(TokenKind::Comma))
    {
        consume();
        Token nextTok = expect(TokenKind::Identifier);
        stmt->vars.push_back(nextTok.lexeme);
    }

    return stmt;
}

/// @brief Parse a LINE INPUT # statement that reads from a file channel.
/// @return LineInputChStmt capturing channel and destination.
StmtPtr Parser::parseLineInput()
{
    auto loc = peek().loc;
    consume(); // LINE
    expect(TokenKind::KeywordInput);
    expect(TokenKind::Hash);
    auto stmt = std::make_unique<LineInputChStmt>();
    stmt->loc = loc;
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    stmt->targetVar = parsePrimary();
    return stmt;
}

/// @brief Parse a RESUME statement controlling error resumption.
/// @return Resume node describing the desired resumption mode.
StmtPtr Parser::parseResume()
{
    auto loc = peek().loc;
    consume(); // RESUME
    auto stmt = std::make_unique<Resume>();
    stmt->loc = loc;
    if (at(TokenKind::KeywordNext))
    {
        consume();
        stmt->mode = Resume::Mode::Next;
    }
    else if (!(at(TokenKind::EndOfLine) || at(TokenKind::EndOfFile) || at(TokenKind::Colon) ||
               isStatementStart(peek().kind)))
    {
        Token labelTok = peek();
        int target = std::atoi(labelTok.lexeme.c_str());
        expect(TokenKind::Number);
        stmt->mode = Resume::Mode::Label;
        stmt->target = target;
    }
    return stmt;
}

/// @brief Parse a DIM declaration for an array or typed scalar.
/// @return DimStmt containing declaration metadata.
/// @note Registers array names in the parser's array set.
StmtPtr Parser::parseDim()
{
    auto loc = peek().loc;
    consume(); // DIM
    Token nameTok = expect(TokenKind::Identifier);
    auto stmt = std::make_unique<DimStmt>();
    stmt->loc = loc;
    stmt->name = nameTok.lexeme;
    stmt->type = typeFromSuffix(nameTok.lexeme);
    if (at(TokenKind::LParen))
    {
        stmt->isArray = true;
        consume();
        stmt->size = parseExpression();
        expect(TokenKind::RParen);
        if (at(TokenKind::KeywordAs))
        {
            consume();
            stmt->type = parseTypeKeyword();
        }
        arrays_.insert(stmt->name);
    }
    else
    {
        stmt->isArray = false;
        if (at(TokenKind::KeywordAs))
        {
            consume();
            stmt->type = parseTypeKeyword();
        }
    }
    return stmt;
}

/// @brief Parse a REDIM statement resizing an array.
/// @return ReDimStmt with name and new size expression.
StmtPtr Parser::parseReDim()
{
    auto loc = peek().loc;
    consume(); // REDIM
    Token nameTok = expect(TokenKind::Identifier);
    expect(TokenKind::LParen);
    auto size = parseExpression();
    expect(TokenKind::RParen);
    auto stmt = std::make_unique<ReDimStmt>();
    stmt->loc = loc;
    stmt->name = nameTok.lexeme;
    stmt->size = std::move(size);
    arrays_.insert(stmt->name);
    return stmt;
}

/// @brief Parse a RANDOMIZE statement setting the PRNG seed.
/// @return RandomizeStmt with the seed expression.
StmtPtr Parser::parseRandomize()
{
    auto loc = peek().loc;
    consume(); // RANDOMIZE
    auto stmt = std::make_unique<RandomizeStmt>();
    stmt->loc = loc;
    stmt->seed = parseExpression();
    return stmt;
}

/// @brief Parse a CLS statement clearing the screen.
/// @return ClsStmt node without additional operands.
StmtPtr Parser::parseCls()
{
    auto loc = consume().loc; // CLS
    auto n = std::make_unique<ClsStmt>();
    n->loc = loc;
    return n;
}

/// @brief Parse a COLOR statement configuring foreground/background colors.
/// @return ColorStmt capturing optional background expression.
StmtPtr Parser::parseColor()
{
    auto loc = consume().loc; // COLOR
    auto n = std::make_unique<ColorStmt>();
    n->loc = loc;
    n->fg = parseExpression();
    if (at(TokenKind::Comma))
    {
        consume();
        n->bg = parseExpression();
    }
    return n;
}

/// @brief Parse a LOCATE statement positioning the cursor.
/// @return LocateStmt capturing row and optional column expressions.
StmtPtr Parser::parseLocate()
{
    auto loc = consume().loc; // LOCATE
    auto n = std::make_unique<LocateStmt>();
    n->loc = loc;
    n->row = parseExpression();
    if (at(TokenKind::Comma))
    {
        consume();
        n->col = parseExpression();
    }
    return n;
}

/// @brief Derive a BASIC type from an identifier suffix.
/// @param name Identifier to inspect.
/// @return Corresponding BASIC type; defaults to I64.
Type Parser::typeFromSuffix(std::string_view name)
{
    if (!name.empty())
    {
        char c = name.back();
        switch (c)
        {
            case '#':
            case '!':
                return Type::F64;
            case '$':
                return Type::Str;
            case '%':
            case '&':
                return Type::I64;
            default:
                break;
        }
    }
    return Type::I64;
}

/// @brief Parse a type keyword following an AS clause.
/// @return BASIC type corresponding to the recognized keyword; defaults to Type::I64.
/// @details Supported keywords: BOOLEAN, INTEGER, DOUBLE, STRING. BOOLEAN is consumed directly
/// by keyword, while the others are matched as identifiers. If no supported keyword is present or
/// an unknown identifier is encountered, the token is treated as INTEGER semantics and Type::I64 is
/// returned without emitting diagnostics, leaving callers to rely on default typing rules.
Type Parser::parseTypeKeyword()
{
    if (at(TokenKind::KeywordBoolean))
    {
        consume();
        return Type::Bool;
    }
    if (at(TokenKind::Identifier))
    {
        std::string name = peek().lexeme;
        consume();
        if (name == "INTEGER")
            return Type::I64;
        if (name == "DOUBLE")
            return Type::F64;
        if (name == "SINGLE")
            return Type::F64;
        if (name == "STRING")
            return Type::Str;
    }
    return Type::I64;
}

/// @brief Parse a parenthesized parameter list.
/// @return Vector of parameters, possibly empty if no list is present.
/// @details Accepts comma-separated identifiers with optional trailing "()" to mark arrays and type
/// suffix characters to infer BASIC types. When no opening parenthesis is found the function
/// returns immediately without consuming tokens. Token mismatches are diagnosed via expect(),
/// allowing the caller to surface parser errors consistently.
std::vector<Param> Parser::parseParamList()
{
    std::vector<Param> params;
    if (!at(TokenKind::LParen))
        return params;
    consume(); // (
    if (at(TokenKind::RParen))
    {
        consume();
        return params;
    }
    while (true)
    {
        Token id = expect(TokenKind::Identifier);
        Param p;
        p.loc = id.loc;
        p.name = id.lexeme;
        p.type = typeFromSuffix(id.lexeme);
        if (at(TokenKind::LParen))
        {
            consume();
            expect(TokenKind::RParen);
            p.is_array = true;
        }
        params.push_back(std::move(p));
        if (at(TokenKind::Comma))
        {
            consume();
            continue;
        }
        break;
    }
    expect(TokenKind::RParen);
    return params;
}

/// @brief Parse the header of a FUNCTION declaration.
/// @return FunctionDecl populated with name, return type, and parameters.
std::unique_ptr<FunctionDecl> Parser::parseFunctionHeader()
{
    auto loc = peek().loc;
    consume(); // FUNCTION
    Token nameTok = expect(TokenKind::Identifier);
    auto fn = std::make_unique<FunctionDecl>();
    fn->loc = loc;
    fn->name = nameTok.lexeme;
    fn->ret = typeFromSuffix(nameTok.lexeme);
    fn->params = parseParamList();
    return fn;
}

/// @brief Shared helper that parses procedure bodies terminated by END.
/// @param endKind Keyword expected after END to close the body.
/// @param body Destination vector for parsed statements.
/// @return Location of the END keyword token.
il::support::SourceLoc Parser::parseProcedureBody(TokenKind endKind, std::vector<StmtPtr> &body)
{
    auto ctx = statementSequencer();
    auto info = ctx.collectStatements(
        [&](int, il::support::SourceLoc)
        { return at(TokenKind::KeywordEnd) && peek(1).kind == endKind; },
        [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
        {
            consume();
            consume();
        },
        body);
    return info.loc;
}

/// @brief Parse statements comprising a function body.
/// @param fn FunctionDecl to populate with body statements.
/// @note Consumes tokens until reaching END FUNCTION.
void Parser::parseFunctionBody(FunctionDecl *fn)
{
    fn->endLoc = parseProcedureBody(TokenKind::KeywordFunction, fn->body);
}

/// @brief Parse a full FUNCTION declaration.
/// @return FunctionDecl statement node including body.
StmtPtr Parser::parseFunction()
{
    auto fn = parseFunctionHeader();
    noteProcedureName(fn->name);
    parseFunctionBody(fn.get());
    return fn;
}

/// @brief Parse a full SUB procedure declaration.
/// @return SubDecl statement node including body.
StmtPtr Parser::parseSub()
{
    auto loc = peek().loc;
    consume(); // SUB
    Token nameTok = expect(TokenKind::Identifier);
    auto sub = std::make_unique<SubDecl>();
    sub->loc = loc;
    sub->name = nameTok.lexeme;
    sub->params = parseParamList();
    noteProcedureName(sub->name);
    parseProcedureBody(TokenKind::KeywordSub, sub->body);
    return sub;
}

/// @brief Parse a RETURN statement.
/// @return ReturnStmt with optional return value expression.
StmtPtr Parser::parseReturn()
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
