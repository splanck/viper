// File: src/frontends/basic/Parser.cpp
// Purpose: Implements BASIC statement parser using token helpers.
// Key invariants: Relies on token buffer for lookahead.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// Links: docs/class-catalog.md

#include "frontends/basic/Parser.hpp"
#include <cstdlib>

namespace il::frontends::basic
{

Parser::Parser(std::string_view src, uint32_t file_id, DiagnosticEmitter *emitter)
    : lexer_(src, file_id), emitter_(emitter)
{
    tokens_.push_back(lexer_.next());
}

std::unique_ptr<Program> Parser::parseProgram()
{
    auto prog = std::make_unique<Program>();
    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();
        if (at(TokenKind::EndOfFile))
            break;
        int line = 0;
        if (at(TokenKind::Number))
        {
            line = std::atoi(peek().lexeme.c_str());
            consume();
        }
        std::vector<StmtPtr> stmts;
        while (true)
        {
            auto stmt = parseStatement(line);
            stmt->line = line;
            stmts.push_back(std::move(stmt));
            if (at(TokenKind::Colon))
            {
                consume();
                continue;
            }
            break;
        }
        if (stmts.size() == 1)
        {
            prog->statements.push_back(std::move(stmts.front()));
        }
        else
        {
            il::support::SourceLoc loc = stmts.front()->loc;
            auto list = std::make_unique<StmtList>();
            list->line = line;
            list->loc = loc;
            list->stmts = std::move(stmts);
            prog->statements.push_back(std::move(list));
        }
        if (at(TokenKind::EndOfLine))
            consume();
    }
    return prog;
}

StmtPtr Parser::parseStatement(int line)
{
    if (at(TokenKind::KeywordPrint))
        return parsePrint();
    if (at(TokenKind::KeywordLet))
        return parseLet();
    if (at(TokenKind::KeywordIf))
        return parseIf(line);
    if (at(TokenKind::KeywordWhile))
        return parseWhile();
    if (at(TokenKind::KeywordFor))
        return parseFor();
    if (at(TokenKind::KeywordNext))
        return parseNext();
    if (at(TokenKind::KeywordGoto))
        return parseGoto();
    if (at(TokenKind::KeywordEnd))
        return parseEnd();
    if (at(TokenKind::KeywordInput))
        return parseInput();
    if (at(TokenKind::KeywordDim))
        return parseDim();
    if (at(TokenKind::KeywordRandomize))
        return parseRandomize();
    if (at(TokenKind::KeywordFunction))
        return parseFunction();
    if (at(TokenKind::KeywordSub))
        return parseSub();
    if (at(TokenKind::KeywordReturn))
        return parseReturn();
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = peek().loc;
    return stmt;
}

StmtPtr Parser::parsePrint()
{
    auto loc = peek().loc;
    consume(); // PRINT
    auto stmt = std::make_unique<PrintStmt>();
    stmt->loc = loc;
    while (!at(TokenKind::EndOfLine) && !at(TokenKind::EndOfFile) && !at(TokenKind::Colon))
    {
        TokenKind k = peek().kind;
        if (k >= TokenKind::KeywordPrint && k <= TokenKind::KeywordNot &&
            k != TokenKind::KeywordAnd && k != TokenKind::KeywordOr && k != TokenKind::KeywordNot)
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

StmtPtr Parser::parseIf(int line)
{
    auto loc = peek().loc;
    consume(); // IF
    auto cond = parseExpression();
    expect(TokenKind::KeywordThen);
    auto thenStmt = parseStatement(line);
    std::vector<IfStmt::ElseIf> elseifs;
    StmtPtr elseStmt;
    while (true)
    {
        if (at(TokenKind::KeywordElseIf))
        {
            consume();
            IfStmt::ElseIf ei;
            ei.cond = parseExpression();
            expect(TokenKind::KeywordThen);
            ei.then_branch = parseStatement(line);
            if (ei.then_branch)
                ei.then_branch->line = line;
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
                ei.then_branch = parseStatement(line);
                if (ei.then_branch)
                    ei.then_branch->line = line;
                elseifs.push_back(std::move(ei));
                continue;
            }
            else
            {
                elseStmt = parseStatement(line);
                if (elseStmt)
                    elseStmt->line = line;
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

StmtPtr Parser::parseWhile()
{
    auto loc = peek().loc;
    consume(); // WHILE
    auto cond = parseExpression();
    if (at(TokenKind::EndOfLine))
        consume();
    else if (at(TokenKind::Colon))
        consume();
    auto stmt = std::make_unique<WhileStmt>();
    stmt->loc = loc;
    stmt->cond = std::move(cond);
    while (true)
    {
        while (at(TokenKind::EndOfLine))
            consume();
        if (at(TokenKind::EndOfFile))
            break;
        int innerLine = 0;
        if (at(TokenKind::Number))
        {
            innerLine = std::atoi(peek().lexeme.c_str());
            consume();
        }
        if (at(TokenKind::KeywordWend))
        {
            consume();
            break;
        }
        auto bodyStmt = parseStatement(innerLine);
        bodyStmt->line = innerLine;
        stmt->body.push_back(std::move(bodyStmt));
        if (at(TokenKind::Colon))
            consume();
        else if (at(TokenKind::EndOfLine))
            consume();
    }
    return stmt;
}

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
    if (at(TokenKind::EndOfLine))
        consume();
    else if (at(TokenKind::Colon))
        consume();
    auto stmt = std::make_unique<ForStmt>();
    stmt->loc = loc;
    stmt->var = var;
    stmt->start = std::move(start);
    stmt->end = std::move(end);
    stmt->step = std::move(step);
    while (true)
    {
        while (at(TokenKind::EndOfLine))
            consume();
        if (at(TokenKind::EndOfFile))
            break;
        int innerLine = 0;
        if (at(TokenKind::Number))
        {
            innerLine = std::atoi(peek().lexeme.c_str());
            consume();
        }
        if (at(TokenKind::KeywordNext))
        {
            consume();
            if (at(TokenKind::Identifier))
                consume();
            break;
        }
        auto bodyStmt = parseStatement(innerLine);
        bodyStmt->line = innerLine;
        stmt->body.push_back(std::move(bodyStmt));
        if (at(TokenKind::Colon))
            consume();
        else if (at(TokenKind::EndOfLine))
            consume();
    }
    return stmt;
}

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

StmtPtr Parser::parseEnd()
{
    auto loc = peek().loc;
    consume(); // END
    auto stmt = std::make_unique<EndStmt>();
    stmt->loc = loc;
    return stmt;
}

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

StmtPtr Parser::parseDim()
{
    auto loc = peek().loc;
    consume(); // DIM
    std::string name = peek().lexeme;
    expect(TokenKind::Identifier);
    expect(TokenKind::LParen);
    auto sz = parseExpression();
    expect(TokenKind::RParen);
    auto stmt = std::make_unique<DimStmt>();
    stmt->loc = loc;
    stmt->name = name;
    stmt->size = std::move(sz);
    arrays_.insert(name);
    return stmt;
}

StmtPtr Parser::parseRandomize()
{
    auto loc = peek().loc;
    consume(); // RANDOMIZE
    auto stmt = std::make_unique<RandomizeStmt>();
    stmt->loc = loc;
    stmt->seed = parseExpression();
    return stmt;
}

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

StmtPtr Parser::parseFunction()
{
    auto loc = peek().loc;
    consume(); // FUNCTION
    Token nameTok = expect(TokenKind::Identifier);
    auto fn = std::make_unique<FunctionDecl>();
    fn->loc = loc;
    fn->name = nameTok.lexeme;
    fn->ret = typeFromSuffix(nameTok.lexeme);
    fn->params = parseParamList();
    if (at(TokenKind::EndOfLine))
        consume();
    else if (at(TokenKind::Colon))
        consume();
    while (true)
    {
        while (at(TokenKind::EndOfLine))
            consume();
        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordFunction)
        {
            auto endLoc = peek().loc;
            consume(); // END
            consume(); // FUNCTION
            fn->endLoc = endLoc;
            break;
        }
        int innerLine = 0;
        if (at(TokenKind::Number))
        {
            innerLine = std::atoi(peek().lexeme.c_str());
            consume();
        }
        auto stmt = parseStatement(innerLine);
        stmt->line = innerLine;
        fn->body.push_back(std::move(stmt));
        if (at(TokenKind::Colon))
            consume();
        else if (at(TokenKind::EndOfLine))
            consume();
    }
    return fn;
}

StmtPtr Parser::parseSub()
{
    auto loc = peek().loc;
    consume(); // SUB
    Token nameTok = expect(TokenKind::Identifier);
    auto sub = std::make_unique<SubDecl>();
    sub->loc = loc;
    sub->name = nameTok.lexeme;
    sub->params = parseParamList();
    if (at(TokenKind::EndOfLine))
        consume();
    else if (at(TokenKind::Colon))
        consume();
    while (true)
    {
        while (at(TokenKind::EndOfLine))
            consume();
        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordSub)
        {
            consume();
            consume();
            break;
        }
        int innerLine = 0;
        if (at(TokenKind::Number))
        {
            innerLine = std::atoi(peek().lexeme.c_str());
            consume();
        }
        auto stmt = parseStatement(innerLine);
        stmt->line = innerLine;
        sub->body.push_back(std::move(stmt));
        if (at(TokenKind::Colon))
            consume();
        else if (at(TokenKind::EndOfLine))
            consume();
    }
    return sub;
}

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
