// File: src/frontends/basic/Parser.cpp
// Purpose: Implements BASIC parser building Program with separate procedure and
//          main statement sections.
// Key invariants: Relies on token buffer for lookahead.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// Links: docs/class-catalog.md

#include "frontends/basic/Parser.hpp"
#include <cstdlib>

namespace il::frontends::basic
{
/// @brief Construct a parser for the given source.
/// @param src Full BASIC source to parse.
/// @param file_id Identifier for diagnostics.
/// @param emitter Destination for emitted diagnostics.
/// @note Initializes the token buffer with the first token for lookahead.
Parser::Parser(std::string_view src, uint32_t file_id, DiagnosticEmitter *emitter)
    : lexer_(src, file_id), emitter_(emitter)
{
    tokens_.push_back(lexer_.next());
}

/// @brief Parse the entire BASIC program.
/// @return Root program node with separated procedure and main sections.
/// @note Assumes all procedures appear before the first main statement.
std::unique_ptr<Program> Parser::parseProgram()
{
    auto prog = std::make_unique<Program>();
    prog->loc = peek().loc;
    bool inMain = false;
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
        StmtPtr root;
        if (stmts.size() == 1)
        {
            root = std::move(stmts.front());
        }
        else
        {
            il::support::SourceLoc loc = stmts.front()->loc;
            auto list = std::make_unique<StmtList>();
            list->line = line;
            list->loc = loc;
            list->stmts = std::move(stmts);
            root = std::move(list);
        }
        if (!inMain &&
            (dynamic_cast<FunctionDecl *>(root.get()) || dynamic_cast<SubDecl *>(root.get())))
        {
            prog->procs.push_back(std::move(root));
        }
        else
        {
            inMain = true;
            prog->main.push_back(std::move(root));
        }
        if (at(TokenKind::EndOfLine))
            consume();
    }
    return prog;
}

/// @brief Parse a single statement based on the current token.
/// @param line Line number associated with the statement.
/// @return Parsed statement node or EndStmt for unknown tokens.
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

/// @brief Parse a WHILE loop terminated by WEND.
/// @return WhileStmt with condition and body statements.
/// @note Consumes statements until a matching WEND token.
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

/// @brief Parse a DIM declaration for an array.
/// @return DimStmt containing array name and size expression.
/// @note Registers the array name in the parser's array set.
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

/// @brief Parse a parenthesized parameter list.
/// @return Vector of parameters, possibly empty if no list is present.
/// @note Parameters may carry type suffixes or array markers.
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

/// @brief Parse statements comprising a function body.
/// @param fn FunctionDecl to populate with body statements.
/// @note Consumes tokens until reaching END FUNCTION.
void Parser::parseFunctionBody(FunctionDecl *fn)
{
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
