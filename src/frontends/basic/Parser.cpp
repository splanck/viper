// File: src/frontends/basic/Parser.cpp
// Purpose: Implements BASIC parser converting tokens to AST.
// Key invariants: None.
// Ownership/Lifetime: Parser references tokens managed externally.
// Links: docs/class-catalog.md

#include "frontends/basic/Parser.hpp"
#include <cstdlib>

namespace il::frontends::basic
{

Parser::Parser(std::string_view src, uint32_t file_id) : lexer_(src, file_id)
{
    advance();
}

void Parser::advance()
{
    current_ = lexer_.next();
}

bool Parser::consume(TokenKind k)
{
    if (check(k))
    {
        advance();
        return true;
    }
    return false;
}

std::unique_ptr<Program> Parser::parseProgram()
{
    auto prog = std::make_unique<Program>();
    while (!check(TokenKind::EndOfFile))
    {
        while (check(TokenKind::EndOfLine))
            advance();
        if (check(TokenKind::EndOfFile))
            break;
        int line = 0;
        if (check(TokenKind::Number))
        {
            line = std::atoi(current_.lexeme.c_str());
            advance();
        }
        while (true)
        {
            auto stmt = parseStatement(line);
            stmt->line = line;
            prog->statements.push_back(std::move(stmt));
            if (check(TokenKind::Colon))
            {
                advance();
                continue;
            }
            break;
        }
        if (check(TokenKind::EndOfLine))
            advance();
    }
    return prog;
}

StmtPtr Parser::parseStatement(int line)
{
    if (check(TokenKind::KeywordPrint))
        return parsePrint();
    if (check(TokenKind::KeywordLet))
        return parseLet();
    if (check(TokenKind::KeywordIf))
        return parseIf(line);
    if (check(TokenKind::KeywordWhile))
        return parseWhile();
    if (check(TokenKind::KeywordFor))
        return parseFor();
    if (check(TokenKind::KeywordNext))
        return parseNext();
    if (check(TokenKind::KeywordGoto))
        return parseGoto();
    if (check(TokenKind::KeywordEnd))
        return parseEnd();
    if (check(TokenKind::KeywordInput))
        return parseInput();
    if (check(TokenKind::KeywordDim))
        return parseDim();
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = current_.loc;
    return stmt;
}

StmtPtr Parser::parsePrint()
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // PRINT
    auto e = parseExpression();
    auto stmt = std::make_unique<PrintStmt>();
    stmt->loc = loc;
    stmt->expr = std::move(e);
    return stmt;
}

StmtPtr Parser::parseLet()
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // LET
    auto target = parsePrimary();
    consume(TokenKind::Equal);
    auto e = parseExpression();
    auto stmt = std::make_unique<LetStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    stmt->expr = std::move(e);
    return stmt;
}

StmtPtr Parser::parseIf(int line)
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // IF or ELSEIF
    auto cond = parseExpression();
    consume(TokenKind::KeywordThen);
    auto thenStmt = parseStatement(line);
    StmtPtr elseStmt;
    if (check(TokenKind::KeywordElseIf))
    {
        elseStmt = parseIf(line);
    }
    else if (check(TokenKind::KeywordElse))
    {
        advance();
        elseStmt = parseStatement(line);
    }
    auto stmt = std::make_unique<IfStmt>();
    stmt->loc = loc;
    stmt->cond = std::move(cond);
    stmt->then_branch = std::move(thenStmt);
    stmt->else_branch = std::move(elseStmt);
    if (stmt->then_branch)
        stmt->then_branch->line = line;
    if (stmt->else_branch)
        stmt->else_branch->line = line;
    return stmt;
}

StmtPtr Parser::parseWhile()
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // WHILE
    auto cond = parseExpression();
    if (check(TokenKind::EndOfLine))
        advance();
    else if (check(TokenKind::Colon))
        advance();
    auto stmt = std::make_unique<WhileStmt>();
    stmt->loc = loc;
    stmt->cond = std::move(cond);
    while (true)
    {
        while (check(TokenKind::EndOfLine))
            advance();
        if (check(TokenKind::EndOfFile))
            break;
        int innerLine = 0;
        if (check(TokenKind::Number))
        {
            innerLine = std::atoi(current_.lexeme.c_str());
            advance();
        }
        if (check(TokenKind::KeywordWend))
        {
            advance();
            break;
        }
        auto bodyStmt = parseStatement(innerLine);
        bodyStmt->line = innerLine;
        stmt->body.push_back(std::move(bodyStmt));
        if (check(TokenKind::Colon))
            advance();
        else if (check(TokenKind::EndOfLine))
            advance();
    }
    return stmt;
}

StmtPtr Parser::parseFor()
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // FOR
    std::string var = current_.lexeme;
    consume(TokenKind::Identifier);
    consume(TokenKind::Equal);
    auto start = parseExpression();
    consume(TokenKind::KeywordTo);
    auto end = parseExpression();
    ExprPtr step;
    if (check(TokenKind::KeywordStep))
    {
        advance();
        step = parseExpression();
    }
    if (check(TokenKind::EndOfLine))
        advance();
    else if (check(TokenKind::Colon))
        advance();
    auto stmt = std::make_unique<ForStmt>();
    stmt->loc = loc;
    stmt->var = var;
    stmt->start = std::move(start);
    stmt->end = std::move(end);
    stmt->step = std::move(step);
    while (true)
    {
        while (check(TokenKind::EndOfLine))
            advance();
        if (check(TokenKind::EndOfFile))
            break;
        int innerLine = 0;
        if (check(TokenKind::Number))
        {
            innerLine = std::atoi(current_.lexeme.c_str());
            advance();
        }
        if (check(TokenKind::KeywordNext))
        {
            advance();
            if (check(TokenKind::Identifier))
                advance();
            break;
        }
        auto bodyStmt = parseStatement(innerLine);
        bodyStmt->line = innerLine;
        stmt->body.push_back(std::move(bodyStmt));
        if (check(TokenKind::Colon))
            advance();
        else if (check(TokenKind::EndOfLine))
            advance();
    }
    return stmt;
}

StmtPtr Parser::parseNext()
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // NEXT
    std::string name;
    if (check(TokenKind::Identifier))
    {
        name = current_.lexeme;
        advance();
    }
    auto stmt = std::make_unique<NextStmt>();
    stmt->loc = loc;
    stmt->var = name;
    return stmt;
}

StmtPtr Parser::parseGoto()
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // GOTO
    int target = std::atoi(current_.lexeme.c_str());
    consume(TokenKind::Number);
    auto stmt = std::make_unique<GotoStmt>();
    stmt->loc = loc;
    stmt->target = target;
    return stmt;
}

StmtPtr Parser::parseEnd()
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // END
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = loc;
    return stmt;
}

StmtPtr Parser::parseInput()
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // INPUT
    std::string name = current_.lexeme;
    consume(TokenKind::Identifier);
    auto stmt = std::make_unique<InputStmt>();
    stmt->loc = loc;
    stmt->var = name;
    return stmt;
}

StmtPtr Parser::parseDim()
{
    il::support::SourceLoc loc = current_.loc;
    advance(); // DIM
    std::string name = current_.lexeme;
    consume(TokenKind::Identifier);
    consume(TokenKind::LParen);
    auto sz = parseExpression();
    consume(TokenKind::RParen);
    auto stmt = std::make_unique<DimStmt>();
    stmt->loc = loc;
    stmt->name = name;
    stmt->size = std::move(sz);
    return stmt;
}

int Parser::precedence(TokenKind k)
{
    switch (k)
    {
        case TokenKind::Star:
        case TokenKind::Slash:
            return 5;
        case TokenKind::Plus:
        case TokenKind::Minus:
            return 4;
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return 3;
        case TokenKind::KeywordAnd:
            return 2;
        case TokenKind::KeywordOr:
            return 1;
        default:
            return 0;
    }
}

ExprPtr Parser::parseExpression(int min_prec)
{
    auto left = parsePrimary();
    while (true)
    {
        int prec = precedence(current_.kind);
        if (prec < min_prec || prec == 0)
            break;
        TokenKind op = current_.kind;
        il::support::SourceLoc opLoc = current_.loc;
        advance();
        auto right = parseExpression(prec + 1);
        auto bin = std::make_unique<BinaryExpr>();
        bin->loc = opLoc;
        switch (op)
        {
            case TokenKind::Plus:
                bin->op = BinaryExpr::Op::Add;
                break;
            case TokenKind::Minus:
                bin->op = BinaryExpr::Op::Sub;
                break;
            case TokenKind::Star:
                bin->op = BinaryExpr::Op::Mul;
                break;
            case TokenKind::Slash:
                bin->op = BinaryExpr::Op::Div;
                break;
            case TokenKind::Equal:
                bin->op = BinaryExpr::Op::Eq;
                break;
            case TokenKind::NotEqual:
                bin->op = BinaryExpr::Op::Ne;
                break;
            case TokenKind::Less:
                bin->op = BinaryExpr::Op::Lt;
                break;
            case TokenKind::LessEqual:
                bin->op = BinaryExpr::Op::Le;
                break;
            case TokenKind::Greater:
                bin->op = BinaryExpr::Op::Gt;
                break;
            case TokenKind::GreaterEqual:
                bin->op = BinaryExpr::Op::Ge;
                break;
            case TokenKind::KeywordAnd:
                bin->op = BinaryExpr::Op::And;
                break;
            case TokenKind::KeywordOr:
                bin->op = BinaryExpr::Op::Or;
                break;
            default:
                bin->op = BinaryExpr::Op::Add;
        }
        bin->lhs = std::move(left);
        bin->rhs = std::move(right);
        left = std::move(bin);
    }
    return left;
}

ExprPtr Parser::parsePrimary()
{
    if (check(TokenKind::Number))
    {
        int v = std::atoi(current_.lexeme.c_str());
        auto loc = current_.loc;
        auto e = std::make_unique<IntExpr>();
        e->loc = loc;
        e->value = v;
        advance();
        return e;
    }
    if (check(TokenKind::String))
    {
        auto loc = current_.loc;
        auto e = std::make_unique<StringExpr>();
        e->loc = loc;
        e->value = current_.lexeme;
        advance();
        return e;
    }
    if (check(TokenKind::KeywordNot))
    {
        il::support::SourceLoc loc = current_.loc;
        advance();
        auto operand = parsePrimary();
        auto e = std::make_unique<UnaryExpr>();
        e->loc = loc;
        e->op = UnaryExpr::Op::Not;
        e->expr = std::move(operand);
        return e;
    }
    if (check(TokenKind::Identifier))
    {
        std::string name = current_.lexeme;
        il::support::SourceLoc loc = current_.loc;
        advance();
        if (consume(TokenKind::LParen))
        {
            if (name == "LEN" || name == "MID$")
            {
                std::vector<ExprPtr> args;
                if (!check(TokenKind::RParen))
                {
                    while (true)
                    {
                        args.push_back(parseExpression());
                        if (consume(TokenKind::Comma))
                            continue;
                        break;
                    }
                }
                consume(TokenKind::RParen);
                auto call = std::make_unique<CallExpr>();
                call->loc = loc;
                if (name == "LEN")
                    call->builtin = CallExpr::Builtin::Len;
                else
                    call->builtin = CallExpr::Builtin::Mid;
                call->args = std::move(args);
                return call;
            }
            else
            {
                auto idx = parseExpression();
                consume(TokenKind::RParen);
                auto arr = std::make_unique<ArrayExpr>();
                arr->loc = loc;
                arr->name = name;
                arr->index = std::move(idx);
                return arr;
            }
        }
        auto v = std::make_unique<VarExpr>();
        v->loc = loc;
        v->name = name;
        return v;
    }
    if (consume(TokenKind::LParen))
    {
        auto e = parseExpression();
        consume(TokenKind::RParen);
        return e;
    }
    auto e = std::make_unique<IntExpr>();
    e->loc = current_.loc;
    e->value = 0;
    return e;
}

} // namespace il::frontends::basic
