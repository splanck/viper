// File: src/frontends/basic/Parser_Stmt.cpp
// Purpose: Implements statement-level parsing routines for the BASIC parser.
// Key invariants: Relies on token buffer for lookahead.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// License: MIT; see LICENSE for details.
// Links: docs/class-catalog.md

#include "frontends/basic/Parser.hpp"
#include <cstdlib>

namespace il::frontends::basic
{

/// @brief Parse a single statement based on the current token.
/// @param line Line number associated with the statement.
/// @return Parsed statement node or EndStmt for unknown tokens.
StmtPtr Parser::parseStatement(int line)
{
    const auto kind = peek().kind;
    const auto index = static_cast<std::size_t>(kind);
    if (index < stmtHandlers_.size())
    {
        const auto &handler = stmtHandlers_[index];
        if (handler.no_arg)
            return (this->*(handler.no_arg))();
        if (handler.with_line)
            return (this->*(handler.with_line))(line);
    }
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = peek().loc;
    return stmt;
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
/// @param ctx Statement context providing newline skipping helpers.
/// @param followerKinds Optional whitelist of tokens that may follow the line label.
void Parser::skipOptionalLineLabelAfterBreak(StatementContext &ctx,
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
/// @param ctx Statement context providing separator helpers.
/// @return Parsed statement representing the branch body.
StmtPtr Parser::parseIfBranchBody(int line, StatementContext &ctx)
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
    auto ctx = statementContext();
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
    auto stmt = std::make_unique<IfStmt>();
    stmt->loc = loc;
    stmt->cond = std::move(cond);
    stmt->then_branch = std::move(thenStmt);
    stmt->elseifs = std::move(elseifs);
    stmt->else_branch = std::move(elseStmt);
    if (stmt->then_branch)
        stmt->then_branch->line = line;
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
    auto ctxWhile = statementContext();
    ctxWhile.consumeStatementBody(TokenKind::KeywordWend, stmt->body);
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
    auto ctxFor = statementContext();
    ctxFor.consumeStatementBody(
        [&](int) { return at(TokenKind::KeywordNext); },
        [&](int, StatementContext::TerminatorInfo &) {
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
    std::string name = peek().lexeme;
    expect(TokenKind::Identifier);
    auto stmt = std::make_unique<InputStmt>();
    stmt->loc = loc;
    stmt->prompt = std::move(prompt);
    stmt->var = name;
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

/// @brief Derive a BASIC type from an identifier suffix.
/// @param name Identifier to inspect.
/// @return Corresponding BASIC type; defaults to I64.
Type Parser::typeFromSuffix(std::string_view name)
{
    if (!name.empty())
    {
        char c = name.back();
        if (c == '#')
            return Type::F64;
        if (c == '$')
            return Type::Str;
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
    auto ctx = statementContext();
    auto info = ctx.consumeStatementBody(
        [&](int) { return at(TokenKind::KeywordEnd) && peek(1).kind == endKind; },
        [&](int, StatementContext::TerminatorInfo &) {
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
