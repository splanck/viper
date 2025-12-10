//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Parser_Decl.cpp
// Purpose: Declaration parsing for Viper Pascal.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/AST.hpp"

namespace il::frontends::pascal
{


std::vector<std::unique_ptr<Decl>> Parser::parseDeclarations()
{
    std::vector<std::unique_ptr<Decl>> decls;

    while (true)
    {
        if (check(TokenKind::KwConst))
        {
            auto constDecls = parseConstSection();
            for (auto &d : constDecls)
                decls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwType))
        {
            auto typeDecls = parseTypeSection();
            for (auto &d : typeDecls)
                decls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwVar))
        {
            auto varDecls = parseVarSection();
            for (auto &d : varDecls)
                decls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwProcedure))
        {
            auto proc = parseProcedure();
            if (proc)
                decls.push_back(std::move(proc));
        }
        else if (check(TokenKind::KwFunction))
        {
            auto func = parseFunction();
            if (func)
                decls.push_back(std::move(func));
        }
        else if (check(TokenKind::KwConstructor))
        {
            auto ctor = parseConstructor();
            if (ctor)
                decls.push_back(std::move(ctor));
        }
        else if (check(TokenKind::KwDestructor))
        {
            auto dtor = parseDestructor();
            if (dtor)
                decls.push_back(std::move(dtor));
        }
        else
        {
            break;
        }
    }

    return decls;
}

std::vector<std::unique_ptr<Decl>> Parser::parseConstSection()
{
    std::vector<std::unique_ptr<Decl>> decls;

    if (!expect(TokenKind::KwConst, "'const'"))
        return decls;

    // Parse const declarations until we hit another section keyword or begin
    while (check(TokenKind::Identifier))
    {
        auto loc = current_.loc;
        auto name = current_.text;
        advance();

        // Optional type annotation
        std::unique_ptr<TypeNode> type;
        if (match(TokenKind::Colon))
        {
            type = parseType();
        }

        // Expect "="
        if (!expect(TokenKind::Equal, "'='"))
        {
            resyncAfterError();
            continue;
        }

        // Parse value expression
        auto value = parseExpression();
        if (!value)
        {
            resyncAfterError();
            continue;
        }

        // Expect ";"
        if (!expect(TokenKind::Semicolon, "';'"))
        {
            resyncAfterError();
        }

        decls.push_back(std::make_unique<ConstDecl>(
            std::move(name), std::move(value), std::move(type), loc));
    }

    return decls;
}

std::vector<std::unique_ptr<Decl>> Parser::parseTypeSection()
{
    std::vector<std::unique_ptr<Decl>> decls;

    if (!expect(TokenKind::KwType, "'type'"))
        return decls;

    // Parse type declarations until we hit another section keyword or begin
    while (check(TokenKind::Identifier))
    {
        auto loc = current_.loc;
        auto name = current_.text;
        advance();

        // Expect "="
        if (!expect(TokenKind::Equal, "'='"))
        {
            resyncAfterError();
            continue;
        }

        // Check for class or interface
        if (check(TokenKind::KwClass))
        {
            advance();
            auto classDecl = parseClass(name, loc);
            if (classDecl)
                decls.push_back(std::move(classDecl));
            continue;
        }

        if (check(TokenKind::KwInterface))
        {
            advance();
            auto ifaceDecl = parseInterface(name, loc);
            if (ifaceDecl)
                decls.push_back(std::move(ifaceDecl));
            continue;
        }

        // Parse type definition
        auto type = parseType();
        if (!type)
        {
            resyncAfterError();
            continue;
        }

        // Expect ";"
        if (!expect(TokenKind::Semicolon, "';'"))
        {
            resyncAfterError();
        }

        decls.push_back(std::make_unique<TypeDecl>(std::move(name), std::move(type), loc));
    }

    return decls;
}

std::vector<std::unique_ptr<Decl>> Parser::parseVarSection()
{
    std::vector<std::unique_ptr<Decl>> decls;

    if (!expect(TokenKind::KwVar, "'var'"))
        return decls;

    // Parse var declarations until we hit another section keyword or begin
    while (check(TokenKind::Identifier))
    {
        auto loc = current_.loc;

        // Parse identifier list
        auto names = parseIdentList();
        if (names.empty())
        {
            resyncAfterError();
            continue;
        }

        // Expect ":"
        if (!expect(TokenKind::Colon, "':'"))
        {
            resyncAfterError();
            continue;
        }

        // Parse type
        auto type = parseType();
        if (!type)
        {
            resyncAfterError();
            continue;
        }

        // Optional initializer
        std::unique_ptr<Expr> init;
        if (match(TokenKind::Equal))
        {
            init = parseExpression();
            if (!init)
            {
                resyncAfterError();
                continue;
            }
        }

        // Expect ";"
        if (!expect(TokenKind::Semicolon, "';'"))
        {
            resyncAfterError();
        }

        decls.push_back(std::make_unique<VarDecl>(
            std::move(names), std::move(type), std::move(init), loc));
    }

    return decls;
}

std::unique_ptr<Decl> Parser::parseProcedure()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwProcedure, "'procedure'"))
        return nullptr;

    // Parse name - may be ClassName.MethodName for method implementations
    if (!check(TokenKind::Identifier))
    {
        error("expected procedure name");
        return nullptr;
    }
    auto name = current_.text;
    std::string className;
    advance();

    // Check for ClassName.MethodName format
    if (match(TokenKind::Dot))
    {
        // name is actually the class name
        className = name;
        if (!check(TokenKind::Identifier))
        {
            error("expected method name after '.'");
            return nullptr;
        }
        name = current_.text;
        advance();
    }

    // Parse parameters
    std::vector<ParamDecl> params;
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            params = parseParameters();
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    auto decl = std::make_unique<ProcedureDecl>(std::move(name), std::move(params), loc);
    decl->className = std::move(className);

    // Check for forward declaration
    if (check(TokenKind::KwForward))
    {
        advance();
        decl->isForward = true;
        expect(TokenKind::Semicolon, "';'");
        return decl;
    }

    // Parse local declarations
    decl->localDecls = parseDeclarations();

    // Parse body
    if (check(TokenKind::KwBegin))
    {
        decl->body = parseBlock();
        expect(TokenKind::Semicolon, "';'");
    }

    return decl;
}

std::unique_ptr<Decl> Parser::parseFunction()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwFunction, "'function'"))
        return nullptr;

    // Parse name - may be ClassName.MethodName for method implementations
    if (!check(TokenKind::Identifier))
    {
        error("expected function name");
        return nullptr;
    }
    auto name = current_.text;
    std::string className;
    advance();

    // Check for ClassName.MethodName format
    if (match(TokenKind::Dot))
    {
        // name is actually the class name
        className = name;
        if (!check(TokenKind::Identifier))
        {
            error("expected method name after '.'");
            return nullptr;
        }
        name = current_.text;
        advance();
    }

    // Parse parameters
    std::vector<ParamDecl> params;
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            params = parseParameters();
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Expect return type
    if (!expect(TokenKind::Colon, "':'"))
        return nullptr;

    auto returnType = parseType();
    if (!returnType)
        return nullptr;

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    auto decl = std::make_unique<FunctionDecl>(
        std::move(name), std::move(params), std::move(returnType), loc);
    decl->className = std::move(className);

    // Check for forward declaration
    if (check(TokenKind::KwForward))
    {
        advance();
        decl->isForward = true;
        expect(TokenKind::Semicolon, "';'");
        return decl;
    }

    // Parse local declarations
    decl->localDecls = parseDeclarations();

    // Parse body
    if (check(TokenKind::KwBegin))
    {
        decl->body = parseBlock();
        expect(TokenKind::Semicolon, "';'");
    }

    return decl;
}

std::vector<ParamDecl> Parser::parseParameters()
{
    std::vector<ParamDecl> params;

    // Parse first parameter group
    auto group = parseParamGroup();
    for (auto &p : group)
        params.push_back(std::move(p));

    // Parse remaining parameter groups
    while (match(TokenKind::Semicolon))
    {
        group = parseParamGroup();
        for (auto &p : group)
            params.push_back(std::move(p));
    }

    return params;
}

std::vector<ParamDecl> Parser::parseParamGroup()
{
    std::vector<ParamDecl> params;
    auto loc = current_.loc;

    // Check for var/const modifier
    bool isVar = false;
    bool isConst = false;
    if (match(TokenKind::KwVar))
    {
        isVar = true;
    }
    else if (match(TokenKind::KwConst))
    {
        isConst = true;
    }

    // Parse identifier list
    auto names = parseIdentList();
    if (names.empty())
        return params;

    // Expect ":"
    if (!expect(TokenKind::Colon, "':'"))
        return params;

    // Parse type
    auto type = parseType();
    if (!type)
        return params;

    // Optional default value
    std::unique_ptr<Expr> defaultValue;
    if (match(TokenKind::Equal))
    {
        defaultValue = parseExpression();
    }

    // Create a ParamDecl for each name
    for (size_t i = 0; i < names.size(); ++i)
    {
        ParamDecl pd;
        pd.name = std::move(names[i]);
        pd.isVar = isVar;
        pd.isConst = isConst;
        pd.loc = loc;

        // Clone type for all but last
        if (i + 1 < names.size())
        {
            // Simple clone for named types
            if (type->kind == TypeKind::Named)
            {
                pd.type = std::make_unique<NamedTypeNode>(
                    static_cast<NamedTypeNode *>(type.get())->name, type->loc);
            }
            else
            {
                // For complex types, just reference the same type
                // Semantic analysis will handle this
                pd.type = std::make_unique<NamedTypeNode>("?", type->loc);
            }
        }
        else
        {
            pd.type = std::move(type);
            if (defaultValue)
                pd.defaultValue = std::move(defaultValue);
        }

        params.push_back(std::move(pd));
    }

    return params;
}


} // namespace il::frontends::pascal

