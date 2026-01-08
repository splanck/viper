//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Parser_Decl.cpp
/// @brief Declaration parsing implementation for the ViperLang parser.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Parser.hpp"

namespace il::frontends::viperlang
{

//===----------------------------------------------------------------------===//
// Declaration Parsing
//===----------------------------------------------------------------------===//

std::unique_ptr<ModuleDecl> Parser::parseModule()
{
    // module Name;
    Token moduleTok;
    if (!expect(TokenKind::KwModule, "module", &moduleTok))
        return nullptr;
    SourceLoc loc = moduleTok.loc;

    Token nameTok;
    if (!expect(TokenKind::Identifier, "module name", &nameTok))
        return nullptr;
    std::string name = nameTok.text;

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    auto module = std::make_unique<ModuleDecl>(loc, std::move(name));

    // Parse imports
    while (check(TokenKind::KwImport))
    {
        module->imports.push_back(parseImportDecl());
    }

    // Parse declarations
    while (!check(TokenKind::Eof))
    {
        // Skip any stray closing braces left over from error recovery.
        // This prevents infinite loops when parse errors leave unmatched braces.
        if (check(TokenKind::RBrace))
        {
            error("unexpected '}' at module level");
            advance();
            continue;
        }

        DeclPtr decl = parseDeclaration();
        if (!decl)
        {
            resyncAfterError();
            continue;
        }
        module->declarations.push_back(std::move(decl));
    }

    return module;
}

ImportDecl Parser::parseImportDecl()
{
    Token importTok = advance(); // consume 'import'
    SourceLoc loc = importTok.loc;

    std::string path;

    // Check for string literal (file path import)
    if (check(TokenKind::StringLiteral))
    {
        Token pathTok = advance();
        path = pathTok.stringValue;
    }
    // Otherwise parse dotted identifier path: Viper.IO.File
    else if (check(TokenKind::Identifier))
    {
        Token firstTok = advance();
        path = firstTok.text;

        while (match(TokenKind::Dot))
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected identifier in import path");
                return ImportDecl(loc, path);
            }
            path += ".";
            Token segmentTok = advance();
            path += segmentTok.text;
        }
    }
    else
    {
        error("expected import path (string or identifier)");
        return ImportDecl(loc, "");
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return ImportDecl(loc, path);

    return ImportDecl(loc, path);
}

DeclPtr Parser::parseDeclaration()
{
    if (check(TokenKind::KwFunc))
    {
        return parseFunctionDecl();
    }
    if (check(TokenKind::KwValue))
    {
        return parseValueDecl();
    }
    if (check(TokenKind::KwEntity))
    {
        return parseEntityDecl();
    }
    if (check(TokenKind::KwInterface))
    {
        return parseInterfaceDecl();
    }
    // Module-level variable declarations (global variables)
    if (check(TokenKind::KwVar) || check(TokenKind::KwFinal))
    {
        return parseGlobalVarDecl();
    }
    // Java-style: Integer x = 5; List[Integer] items = []; Entity? e = null;
    if (check(TokenKind::Identifier))
    {
        Speculation speculative(*this);
        if (DeclPtr decl = parseJavaStyleGlobalVarDecl())
        {
            speculative.commit();
            return decl;
        }
    }

    error("expected declaration");
    return nullptr;
}

DeclPtr Parser::parseFunctionDecl()
{
    Token funcTok = advance(); // consume 'func'
    SourceLoc loc = funcTok.loc;

    if (!check(TokenKind::Identifier))
    {
        error("expected function name");
        return nullptr;
    }
    Token nameTok = advance();
    std::string name = nameTok.text;

    auto func = std::make_unique<FunctionDecl>(loc, std::move(name));

    // Generic parameters
    func->genericParams = parseGenericParams();

    // Parameters
    if (!expect(TokenKind::LParen, "("))
        return nullptr;

    func->params = parseParameters();

    if (!expect(TokenKind::RParen, ")"))
        return nullptr;

    // Return type (supports both -> Type and : Type syntax)
    if (match(TokenKind::Arrow) || match(TokenKind::Colon))
    {
        func->returnType = parseType();
        if (!func->returnType)
            return nullptr;
    }

    // Body
    if (check(TokenKind::LBrace))
    {
        func->body = parseBlock();
        if (!func->body)
            return nullptr;
    }
    else
    {
        error("expected function body");
        return nullptr;
    }

    return func;
}

std::vector<Param> Parser::parseParameters()
{
    std::vector<Param> params;

    if (check(TokenKind::RParen))
    {
        return params;
    }

    do
    {
        Param param;

        if (!checkIdentifierLike())
        {
            error("expected parameter");
            return {};
        }

        // Read first identifier (may be a contextual keyword like 'value')
        Token firstTok = advance();
        std::string first = firstTok.text;
        SourceLoc firstLoc = firstTok.loc;

        if (check(TokenKind::Colon))
        {
            // Swift style: name: Type
            advance(); // consume :
            param.name = first;
            param.type = parseType();
            if (!param.type)
                return {};
        }
        else if (checkIdentifierLike())
        {
            // Java style: Type name (name can be contextual keyword like 'value')
            Token nameTok = advance();
            param.name = nameTok.text;
            param.type = std::make_unique<NamedType>(firstLoc, first);
        }
        else if (match(TokenKind::LBracket))
        {
            // Generic type Java style: List[T] name
            std::vector<TypePtr> typeArgs;
            do
            {
                TypePtr arg = parseType();
                if (!arg)
                    return {};
                typeArgs.push_back(std::move(arg));
            } while (match(TokenKind::Comma));

            if (!expect(TokenKind::RBracket, "]"))
                return {};

            // Now parse the parameter name (can be contextual keyword like 'value')
            if (!checkIdentifierLike())
            {
                error("expected parameter name after type");
                return {};
            }
            Token nameTok = advance();
            param.name = nameTok.text;
            param.type = std::make_unique<GenericType>(firstLoc, first, std::move(typeArgs));
        }
        else if (match(TokenKind::Question))
        {
            // Optional type Java style: Type? name
            if (!checkIdentifierLike())
            {
                error("expected parameter name after type");
                return {};
            }
            Token nameTok = advance();
            param.name = nameTok.text;
            auto baseType = std::make_unique<NamedType>(firstLoc, first);
            param.type = std::make_unique<OptionalType>(firstLoc, std::move(baseType));
        }
        else
        {
            error("expected ':' or parameter name");
            return {};
        }

        // Default value
        if (match(TokenKind::Equal))
        {
            param.defaultValue = parseExpression();
            if (!param.defaultValue)
                return {};
        }

        params.push_back(std::move(param));
    } while (match(TokenKind::Comma));

    return params;
}

std::vector<std::string> Parser::parseGenericParams()
{
    std::vector<std::string> params;

    if (!match(TokenKind::LBracket))
    {
        return params;
    }

    do
    {
        if (!check(TokenKind::Identifier))
        {
            error("expected type parameter name");
            return {};
        }
        Token nameTok = advance();
        params.push_back(nameTok.text);
    } while (match(TokenKind::Comma));

    if (!expect(TokenKind::RBracket, "]"))
        return {};

    return params;
}

DeclPtr Parser::parseValueDecl()
{
    Token valueTok = advance(); // consume 'value'
    SourceLoc loc = valueTok.loc;

    if (!check(TokenKind::Identifier))
    {
        error("expected value type name");
        return nullptr;
    }
    Token nameTok = advance();
    std::string name = nameTok.text;

    auto value = std::make_unique<ValueDecl>(loc, std::move(name));

    // Generic parameters
    value->genericParams = parseGenericParams();

    // Implements clause
    if (match(TokenKind::KwImplements))
    {
        do
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected interface name");
                return nullptr;
            }
            Token ifaceTok = advance();
            value->interfaces.push_back(ifaceTok.text);
        } while (match(TokenKind::Comma));
    }

    // Body
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    // Parse members (fields and methods)
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        // Check for visibility modifier
        Visibility visibility = Visibility::Public; // Default for value types
        if (check(TokenKind::KwExpose))
        {
            visibility = Visibility::Public;
            advance();
        }
        else if (check(TokenKind::KwHide))
        {
            visibility = Visibility::Private;
            advance();
        }

        if (check(TokenKind::KwFunc))
        {
            // Method declaration
            auto method = parseMethodDecl();
            if (method)
            {
                static_cast<MethodDecl *>(method.get())->visibility = visibility;
                value->members.push_back(std::move(method));
            }
        }
        else if (check(TokenKind::Identifier))
        {
            // Field declaration: TypeName fieldName;
            auto field = parseFieldDecl();
            if (field)
            {
                static_cast<FieldDecl *>(field.get())->visibility = visibility;
                value->members.push_back(std::move(field));
            }
        }
        else
        {
            error("expected field or method declaration");
            advance();
        }
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return value;
}

DeclPtr Parser::parseEntityDecl()
{
    Token entityTok = advance(); // consume 'entity'
    SourceLoc loc = entityTok.loc;

    if (!check(TokenKind::Identifier))
    {
        error("expected entity type name");
        return nullptr;
    }
    Token nameTok = advance();
    std::string name = nameTok.text;

    auto entity = std::make_unique<EntityDecl>(loc, std::move(name));

    // Generic parameters
    entity->genericParams = parseGenericParams();

    // Extends clause
    if (match(TokenKind::KwExtends))
    {
        if (!check(TokenKind::Identifier))
        {
            error("expected base class name");
            return nullptr;
        }
        Token baseTok = advance();
        entity->baseClass = baseTok.text;
    }

    // Implements clause
    if (match(TokenKind::KwImplements))
    {
        do
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected interface name");
                return nullptr;
            }
            Token ifaceTok = advance();
            entity->interfaces.push_back(ifaceTok.text);
        } while (match(TokenKind::Comma));
    }

    // Body
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    // Parse members (fields and methods)
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        // Check for modifiers (can appear in any order)
        Visibility visibility = Visibility::Private; // Default for entity types
        bool isOverride = false;

        while (check(TokenKind::KwExpose) || check(TokenKind::KwHide) ||
               check(TokenKind::KwOverride))
        {
            if (match(TokenKind::KwExpose))
                visibility = Visibility::Public;
            else if (match(TokenKind::KwHide))
                visibility = Visibility::Private;
            else if (match(TokenKind::KwOverride))
                isOverride = true;
        }

        if (check(TokenKind::KwFunc))
        {
            // Method declaration
            auto method = parseMethodDecl();
            if (method)
            {
                auto *m = static_cast<MethodDecl *>(method.get());
                m->visibility = visibility;
                m->isOverride = isOverride;
                entity->members.push_back(std::move(method));
            }
        }
        else if (check(TokenKind::Identifier))
        {
            // Field declaration: TypeName fieldName;
            auto field = parseFieldDecl();
            if (field)
            {
                static_cast<FieldDecl *>(field.get())->visibility = visibility;
                entity->members.push_back(std::move(field));
            }
        }
        else
        {
            error("expected field or method declaration");
            advance();
        }
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return entity;
}

DeclPtr Parser::parseInterfaceDecl()
{
    Token ifaceTok = advance(); // consume 'interface'
    SourceLoc loc = ifaceTok.loc;

    if (!check(TokenKind::Identifier))
    {
        error("expected interface name");
        return nullptr;
    }
    Token nameTok = advance();
    std::string name = nameTok.text;

    auto iface = std::make_unique<InterfaceDecl>(loc, std::move(name));

    // Generic parameters
    iface->genericParams = parseGenericParams();

    // Body
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    // Parse method signatures
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        if (check(TokenKind::KwFunc))
        {
            // Parse method signature (method without body)
            auto method = parseMethodDecl();
            if (method)
            {
                static_cast<MethodDecl *>(method.get())->visibility = Visibility::Public;
                iface->members.push_back(std::move(method));
            }
        }
        else
        {
            error("expected method signature in interface");
            advance();
        }
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return iface;
}

DeclPtr Parser::parseGlobalVarDecl()
{
    Token kwTok = advance(); // consume 'var' or 'final'
    SourceLoc loc = kwTok.loc;
    bool isFinal = kwTok.kind == TokenKind::KwFinal;

    if (!check(TokenKind::Identifier))
    {
        error("expected variable name");
        return nullptr;
    }
    Token nameTok = advance();
    std::string name = nameTok.text;

    auto decl = std::make_unique<GlobalVarDecl>(loc, std::move(name));
    decl->isFinal = isFinal;

    // Optional type annotation: var x: Integer
    if (match(TokenKind::Colon))
    {
        decl->type = parseType();
        if (!decl->type)
            return nullptr;
    }

    // Optional initializer: var x = 42
    if (match(TokenKind::Equal))
    {
        decl->initializer = parseExpression();
        if (!decl->initializer)
            return nullptr;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return decl;
}

DeclPtr Parser::parseJavaStyleGlobalVarDecl()
{
    SourceLoc loc = peek().loc;

    // Parse the type (e.g., Integer, List, etc.)
    TypePtr type = parseType();
    if (!type)
        return nullptr;

    // Now we expect a variable name
    if (!check(TokenKind::Identifier))
    {
        error("expected variable name after type");
        return nullptr;
    }
    Token nameTok = advance();
    std::string name = nameTok.text;

    auto decl = std::make_unique<GlobalVarDecl>(loc, std::move(name));
    decl->type = std::move(type);
    decl->isFinal = false; // Java-style declarations are mutable by default

    // Optional initializer: Integer x = 42
    if (match(TokenKind::Equal))
    {
        decl->initializer = parseExpression();
        if (!decl->initializer)
            return nullptr;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return decl;
}

DeclPtr Parser::parseFieldDecl()
{
    SourceLoc loc = peek().loc;

    // Parse the type (handles generic types like List[Vehicle], optional types, etc.)
    TypePtr type = parseType();
    if (!type)
        return nullptr;

    // Field name
    if (!checkIdentifierLike())
    {
        error("expected field name");
        return nullptr;
    }
    Token nameTok = advance();
    std::string fieldName = nameTok.text;

    auto field = std::make_unique<FieldDecl>(loc, std::move(fieldName));
    field->type = std::move(type);

    // Optional initializer: = expr
    if (match(TokenKind::Equal))
    {
        field->initializer = parseExpression();
    }

    // Expect semicolon
    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return field;
}

DeclPtr Parser::parseMethodDecl()
{
    Token funcTok = advance(); // consume 'func'
    SourceLoc loc = funcTok.loc;

    if (!check(TokenKind::Identifier))
    {
        error("expected method name");
        return nullptr;
    }
    Token nameTok = advance();
    std::string name = nameTok.text;

    auto method = std::make_unique<MethodDecl>(loc, std::move(name));

    // Generic parameters
    method->genericParams = parseGenericParams();

    // Parameters
    if (!expect(TokenKind::LParen, "("))
        return nullptr;
    method->params = parseParameters();
    if (!expect(TokenKind::RParen, ")"))
        return nullptr;

    // Return type (supports both -> Type and : Type syntax)
    if (match(TokenKind::Arrow) || match(TokenKind::Colon))
    {
        method->returnType = parseType();
    }

    // Body
    if (check(TokenKind::LBrace))
    {
        method->body = parseBlock();
    }
    else
    {
        // No body - interface method signature
        if (!expect(TokenKind::Semicolon, ";"))
            return nullptr;
    }

    return method;
}

} // namespace il::frontends::viperlang
