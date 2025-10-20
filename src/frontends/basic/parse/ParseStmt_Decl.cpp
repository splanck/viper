//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements declaration-oriented statement parselets for the BASIC
/// front end.
/// @details Hosts parsing routines for LET assignments, procedure declarations,
/// and user-defined types so that the primary statement dispatcher can remain
/// focused on keyword routing.

#include "frontends/basic/Parser.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

StmtPtr Parser::parseLetStatement()
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

StmtPtr Parser::parseClassDecl()
{
    auto loc = peek().loc;
    consume(); // CLASS

    Token nameTok = expect(TokenKind::Identifier);

    auto decl = std::make_unique<ClassDecl>();
    decl->loc = loc;
    if (nameTok.kind == TokenKind::Identifier)
        decl->name = nameTok.lexeme;

    auto equalsIgnoreCase = [](const std::string &lhs, std::string_view rhs) {
        if (lhs.size() != rhs.size())
            return false;
        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            unsigned char lc = static_cast<unsigned char>(lhs[i]);
            unsigned char rc = static_cast<unsigned char>(rhs[i]);
            if (std::toupper(lc) != std::toupper(rc))
                return false;
        }
        return true;
    };

    if (at(TokenKind::EndOfLine))
        consume();

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordClass)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::Identifier && peek(2).kind == TokenKind::KeywordAs)
            {
                consume();
                continue;
            }
        }

        if (!(at(TokenKind::Identifier) && peek(1).kind == TokenKind::KeywordAs))
            break;

        Token fieldNameTok = expect(TokenKind::Identifier);
        if (fieldNameTok.kind != TokenKind::Identifier)
            break;

        Token asTok = expect(TokenKind::KeywordAs);
        if (asTok.kind != TokenKind::KeywordAs)
            continue;

        Type fieldType = Type::I64;
        if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
        {
            fieldType = parseTypeKeyword();
        }
        else
        {
            expect(TokenKind::Identifier);
        }

        ClassDecl::Field field;
        field.name = fieldNameTok.lexeme;
        field.type = fieldType;
        decl->fields.push_back(std::move(field));

        if (at(TokenKind::EndOfLine))
            consume();
    }

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordClass)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::KeywordSub || nextKind == TokenKind::KeywordFunction ||
                nextKind == TokenKind::KeywordDestructor ||
                (nextKind == TokenKind::KeywordEnd && peek(2).kind == TokenKind::KeywordClass))
            {
                consume();
                continue;
            }
        }

        if (at(TokenKind::KeywordSub))
        {
            auto subLoc = peek().loc;
            consume(); // SUB
            Token subNameTok;
            if (peek().kind == TokenKind::KeywordNew)
            {
                subNameTok = peek();
                consume();
                subNameTok.kind = TokenKind::Identifier;
            }
            else
            {
                subNameTok = expect(TokenKind::Identifier);
                if (subNameTok.kind != TokenKind::Identifier)
                    break;
            }

            if (equalsIgnoreCase(subNameTok.lexeme, "NEW"))
            {
                auto ctor = std::make_unique<ConstructorDecl>();
                ctor->loc = subLoc;
                ctor->params = parseParamList();
                parseProcedureBody(TokenKind::KeywordSub, ctor->body);
                decl->members.push_back(std::move(ctor));
                continue;
            }

            auto method = std::make_unique<MethodDecl>();
            method->loc = subLoc;
            method->name = subNameTok.lexeme;
            method->params = parseParamList();
            parseProcedureBody(TokenKind::KeywordSub, method->body);
            decl->members.push_back(std::move(method));
            continue;
        }

        if (at(TokenKind::KeywordFunction))
        {
            auto fnLoc = peek().loc;
            consume(); // FUNCTION
            Token fnNameTok = expect(TokenKind::Identifier);
            if (fnNameTok.kind != TokenKind::Identifier)
                break;

            auto method = std::make_unique<MethodDecl>();
            method->loc = fnLoc;
            method->name = fnNameTok.lexeme;
            method->ret = typeFromSuffix(fnNameTok.lexeme);
            method->params = parseParamList();
            parseProcedureBody(TokenKind::KeywordFunction, method->body);
            decl->members.push_back(std::move(method));
            continue;
        }

        if (at(TokenKind::KeywordDestructor))
        {
            auto dtorLoc = peek().loc;
            consume(); // DESTRUCTOR
            auto dtor = std::make_unique<DestructorDecl>();
            dtor->loc = dtorLoc;
            parseProcedureBody(TokenKind::KeywordDestructor, dtor->body);
            decl->members.push_back(std::move(dtor));
            continue;
        }

        break;
    }

    while (at(TokenKind::EndOfLine))
        consume();

    if (at(TokenKind::Number) && peek(1).kind == TokenKind::KeywordEnd &&
        peek(2).kind == TokenKind::KeywordClass)
    {
        consume();
    }

    expect(TokenKind::KeywordEnd);
    expect(TokenKind::KeywordClass);

    return decl;
}

StmtPtr Parser::parseTypeDecl()
{
    auto loc = peek().loc;
    consume(); // TYPE

    Token nameTok = expect(TokenKind::Identifier);

    auto decl = std::make_unique<TypeDecl>();
    decl->loc = loc;
    if (nameTok.kind == TokenKind::Identifier)
        decl->name = nameTok.lexeme;

    if (at(TokenKind::EndOfLine))
        consume();

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordType)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::Identifier ||
                (nextKind == TokenKind::KeywordEnd && peek(2).kind == TokenKind::KeywordType))
            {
                consume();
                continue;
            }
        }

        Token fieldNameTok = expect(TokenKind::Identifier);
        if (fieldNameTok.kind != TokenKind::Identifier)
            break;

        Token asTok = expect(TokenKind::KeywordAs);
        if (asTok.kind != TokenKind::KeywordAs)
            continue;

        Type fieldType = Type::I64;
        if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
        {
            fieldType = parseTypeKeyword();
        }
        else
        {
            expect(TokenKind::Identifier);
        }

        TypeDecl::Field field;
        field.name = fieldNameTok.lexeme;
        field.type = fieldType;
        decl->fields.push_back(std::move(field));

        if (at(TokenKind::EndOfLine))
            consume();
    }

    while (at(TokenKind::EndOfLine))
        consume();

    if (at(TokenKind::Number) && peek(1).kind == TokenKind::KeywordEnd &&
        peek(2).kind == TokenKind::KeywordType)
    {
        consume();
    }

    expect(TokenKind::KeywordEnd);
    expect(TokenKind::KeywordType);

    return decl;
}

StmtPtr Parser::parseDeleteStatement()
{
    auto loc = peek().loc;
    consume(); // DELETE

    auto target = parseExpression();
    auto stmt = std::make_unique<DeleteStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    return stmt;
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

void Parser::parseFunctionBody(FunctionDecl *fn)
{
    fn->endLoc = parseProcedureBody(TokenKind::KeywordFunction, fn->body);
}

StmtPtr Parser::parseFunctionStatement()
{
    auto fn = parseFunctionHeader();
    noteProcedureName(fn->name);
    parseFunctionBody(fn.get());
    return fn;
}

StmtPtr Parser::parseSubStatement()
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

} // namespace il::frontends::basic

