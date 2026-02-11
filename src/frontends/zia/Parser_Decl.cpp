//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Parser_Decl.cpp
/// @brief Declaration parsing implementation for the Zia parser.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Parser.hpp"

namespace il::frontends::zia
{

//===----------------------------------------------------------------------===//
// Declaration Parsing
//===----------------------------------------------------------------------===//

/// @brief Parse a top-level module declaration (module Name; binds; declarations).
/// @return The parsed ModuleDecl, or nullptr on error.
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

    // Parse binds
    while (check(TokenKind::KwBind))
    {
        module->binds.push_back(parseBindDecl());
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

/// @brief Parse a bind declaration for file or namespace imports.
/// @details Supports string literal paths, dotted namespace paths, alias assignment
///          (Alias = Path), selective imports ({ Item1, Item2 }), and alias imports (as T).
/// @return The parsed BindDecl.
BindDecl Parser::parseBindDecl()
{
    Token bindTok = advance(); // consume 'bind'
    SourceLoc loc = bindTok.loc;

    std::string path;
    bool isNamespaceBind = false;

    // Check for string literal (file path bind)
    if (check(TokenKind::StringLiteral))
    {
        Token pathTok = advance();
        path = pathTok.stringValue;
        isNamespaceBind = false;
    }
    // Otherwise parse dotted identifier path: Viper.Terminal or module_name
    else if (check(TokenKind::Identifier))
    {
        Token firstTok = advance();

        // Check for alias assignment syntax: bind Alias = Viper.Path;
        if (match(TokenKind::Equal))
        {
            std::string alias = firstTok.text;

            if (!check(TokenKind::Identifier))
            {
                error("expected namespace path after '='");
                return BindDecl(loc, "");
            }

            Token pathTok = advance();
            path = pathTok.text;

            while (match(TokenKind::Dot))
            {
                if (!check(TokenKind::Identifier))
                {
                    error("expected identifier in bind path");
                    return BindDecl(loc, path);
                }
                path += ".";
                Token segmentTok = advance();
                path += segmentTok.text;
            }

            if (path.rfind("Viper.", 0) == 0)
                isNamespaceBind = true;

            BindDecl decl(loc, path);
            decl.isNamespaceBind = isNamespaceBind;
            decl.alias = alias;

            if (!expect(TokenKind::Semicolon, ";"))
                return decl;

            return decl;
        }

        // Standard dotted path: bind Viper.Terminal; or bind Viper.Terminal as T;
        path = firstTok.text;

        while (match(TokenKind::Dot))
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected identifier in bind path");
                return BindDecl(loc, path);
            }
            path += ".";
            Token segmentTok = advance();
            path += segmentTok.text;
        }

        // Detect if this is a namespace bind (starts with "Viper.")
        // File binds use string literals, namespace binds use dotted identifiers
        if (path.rfind("Viper.", 0) == 0)
        {
            isNamespaceBind = true;
        }
    }
    else
    {
        error("expected bind path (string or identifier)");
        return BindDecl(loc, "");
    }

    BindDecl decl(loc, path);
    decl.isNamespaceBind = isNamespaceBind;

    // Parse optional selective import: { item1, item2, ... }
    // Only valid for namespace binds
    if (check(TokenKind::LBrace))
    {
        if (!isNamespaceBind)
        {
            error("selective imports { ... } only allowed for namespace binds");
        }
        else
        {
            advance(); // consume '{'
            while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
            {
                if (!check(TokenKind::Identifier))
                {
                    error("expected identifier in selective import list");
                    break;
                }
                Token itemTok = advance();
                decl.specificItems.push_back(itemTok.text);

                if (!match(TokenKind::Comma))
                    break;
            }
            if (!expect(TokenKind::RBrace, "}"))
                return decl;
        }
    }

    // Parse optional alias: as AliasName
    // Note: alias and selective import are mutually exclusive
    if (match(TokenKind::KwAs))
    {
        if (!decl.specificItems.empty())
        {
            error("cannot use alias 'as' with selective imports { ... }");
            return decl;
        }
        if (!check(TokenKind::Identifier))
        {
            error("expected alias name after 'as'");
            return decl;
        }
        Token aliasTok = advance();
        decl.alias = aliasTok.text;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return decl;

    return decl;
}

/// @brief Dispatch to the appropriate declaration parser based on the current keyword.
/// @details Handles func, value, entity, interface, namespace, var/final, and
///          Java-style global variable declarations.
/// @return The parsed declaration, or nullptr on error.
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
    if (check(TokenKind::KwNamespace))
    {
        return parseNamespaceDecl();
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

/// @brief Parse a function declaration (func name[T](...) -> RetType { body }).
/// @return The parsed FunctionDecl, or nullptr on error.
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

    // Generic parameters with optional constraints
    func->genericParams = parseGenericParamsWithConstraints(func->genericParamConstraints);

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

/// @brief Parse a comma-separated list of function or method parameters.
/// @details Supports both Swift-style (name: Type) and Java-style (Type name) parameters,
///          optional types (Type?), generic types (List[T]), and default values.
/// @return The parsed parameter list, or empty vector on error.
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

/// @brief Parse generic type parameters enclosed in brackets ([T, U, ...]).
/// @return The list of type parameter names, or empty if no brackets present.
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

/// @brief Parse generic type parameters with optional constraints ([T: Comparable, U]).
/// @param[out] constraints Parallel vector of constraint interface names (empty string =
/// unconstrained).
/// @return The list of type parameter names, or empty if no brackets present.
std::vector<std::string> Parser::parseGenericParamsWithConstraints(
    std::vector<std::string> &constraints)
{
    std::vector<std::string> params;
    constraints.clear();

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

        // Check for optional constraint: T: ConstraintName
        if (match(TokenKind::Colon))
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected constraint interface name after ':'");
                return {};
            }
            Token constraintTok = advance();
            constraints.push_back(constraintTok.text);
        }
        else
        {
            constraints.push_back(""); // No constraint
        }
    } while (match(TokenKind::Comma));

    if (!expect(TokenKind::RBracket, "]"))
        return {};

    return params;
}

/// @brief Parse a comma-separated interface list after 'implements'.
/// @param[out] interfaces Output vector of interface name strings.
/// @return True on success (or no 'implements' present), false on error.
bool Parser::parseInterfaceList(std::vector<std::string> &interfaces)
{
    if (!match(TokenKind::KwImplements))
        return true;

    do
    {
        if (!check(TokenKind::Identifier))
        {
            error("expected interface name");
            return false;
        }
        Token ifaceTok = advance();
        interfaces.push_back(ifaceTok.text);
    } while (match(TokenKind::Comma));

    return true;
}

/// @brief Parse type body members (fields and methods) between braces.
/// @param[out] members Output member list.
/// @param defaultVisibility Default visibility (Public for value, Private for entity).
/// @param allowOverride Whether the 'override' modifier is permitted.
/// @return True on success.
bool Parser::parseMemberBlock(std::vector<DeclPtr> &members,
                              Visibility defaultVisibility,
                              bool allowOverride)
{
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        Visibility visibility = defaultVisibility;
        bool isOverride = false;

        // Parse modifiers (can appear in any order when override is allowed)
        while (check(TokenKind::KwExpose) || check(TokenKind::KwHide) ||
               (allowOverride && check(TokenKind::KwOverride)))
        {
            if (match(TokenKind::KwExpose))
                visibility = Visibility::Public;
            else if (match(TokenKind::KwHide))
                visibility = Visibility::Private;
            else if (allowOverride && match(TokenKind::KwOverride))
                isOverride = true;
        }

        if (check(TokenKind::KwFunc))
        {
            auto method = parseMethodDecl();
            if (method)
            {
                auto *m = static_cast<MethodDecl *>(method.get());
                m->visibility = visibility;
                m->isOverride = isOverride;
                members.push_back(std::move(method));
            }
        }
        else if (check(TokenKind::Identifier))
        {
            auto field = parseFieldDecl();
            if (field)
            {
                static_cast<FieldDecl *>(field.get())->visibility = visibility;
                members.push_back(std::move(field));
            }
        }
        else
        {
            error("expected field or method declaration");
            advance();
        }
    }

    return true;
}

/// @brief Parse a value type declaration (value Name[T] implements I { fields; methods }).
/// @return The parsed ValueDecl, or nullptr on error.
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
    if (!parseInterfaceList(value->interfaces))
        return nullptr;

    // Body
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    parseMemberBlock(value->members, Visibility::Public, /*allowOverride=*/false);

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return value;
}

/// @brief Parse an entity type declaration (entity Name[T] extends Base implements I { ... }).
/// @return The parsed EntityDecl, or nullptr on error.
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
    if (!parseInterfaceList(entity->interfaces))
        return nullptr;

    // Body
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    parseMemberBlock(entity->members, Visibility::Private, /*allowOverride=*/true);

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return entity;
}

/// @brief Parse an interface declaration (interface Name[T] { method signatures }).
/// @return The parsed InterfaceDecl, or nullptr on error.
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

/// @brief Parse a namespace declaration (namespace Foo.Bar { declarations... }).
/// @return The parsed NamespaceDecl, or nullptr on error.
DeclPtr Parser::parseNamespaceDecl()
{
    Token nsTok = advance(); // consume 'namespace'
    SourceLoc loc = nsTok.loc;

    // Parse namespace name (can be dotted like MyLib.Internal)
    if (!check(TokenKind::Identifier))
    {
        error("expected namespace name");
        return nullptr;
    }

    std::string name = advance().text;

    // Allow dotted names: namespace Foo.Bar.Baz { }
    while (check(TokenKind::Dot))
    {
        advance(); // consume '.'
        if (!check(TokenKind::Identifier))
        {
            error("expected identifier after '.' in namespace name");
            return nullptr;
        }
        name += ".";
        name += advance().text;
    }

    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    auto ns = std::make_unique<NamespaceDecl>(loc, name);

    // Parse declarations inside the namespace
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        if (DeclPtr decl = parseDeclaration())
        {
            ns->declarations.push_back(std::move(decl));
        }
        else
        {
            // Skip to recover
            advance();
        }
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return ns;
}

/// @brief Parse a global variable declaration using var/final syntax.
/// @return The parsed GlobalVarDecl, or nullptr on error.
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

/// @brief Parse a Java-style global variable declaration (Type name = expr;).
/// @details Used speculatively when the current token is an uppercase identifier.
/// @return The parsed GlobalVarDecl, or nullptr if not a valid Java-style declaration.
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

/// @brief Parse a field declaration inside a value or entity body (Type name [= init];).
/// @return The parsed FieldDecl, or nullptr on error.
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

/// @brief Parse a method declaration inside a value, entity, or interface body.
/// @details For interfaces, the method has no body and ends with a semicolon.
/// @return The parsed MethodDecl, or nullptr on error.
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

} // namespace il::frontends::zia
