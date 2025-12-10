//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Parser_OOP.cpp
// Purpose: OOP parsing (class, interface) for Viper Pascal.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/AST.hpp"

namespace il::frontends::pascal
{


std::unique_ptr<Decl> Parser::parseClass(const std::string &name, il::support::SourceLoc loc)
{
    auto decl = std::make_unique<ClassDecl>(name, loc);

    // Optional heritage clause: (BaseClass, Interface1, Interface2)
    // The first identifier could be a base class or an interface.
    // We collect all identifiers and then determine which is the base class
    // based on whether the identifier refers to a class or interface type.
    if (match(TokenKind::LParen))
    {
        std::vector<std::string> heritageNames;
        if (check(TokenKind::Identifier))
        {
            heritageNames.push_back(current_.text);
            advance();

            // Additional identifiers
            while (match(TokenKind::Comma))
            {
                if (!check(TokenKind::Identifier))
                {
                    error("expected type name");
                    break;
                }
                heritageNames.push_back(current_.text);
                advance();
            }
        }

        // Determine which is the base class (if any) and which are interfaces.
        // For now, the first non-interface type is the base class.
        // Since we don't have semantic info in the parser, we rely on convention:
        // - Types starting with 'I' followed by uppercase are likely interfaces
        // - Or we can look ahead at type declarations in the type block
        // For simplicity, we'll check if it's a known interface type by looking
        // at previously parsed interface declarations in this type block.
        for (const auto &name : heritageNames)
        {
            // Check if this is a forward-declared or previously parsed interface
            // by looking for IName pattern (convention) or checking pendingInterfaces_
            bool isInterface = false;

            // Check if we've already seen this as an interface in the current parse
            std::string lowerName = name;
            for (auto &c : lowerName)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            // Simple heuristic: starts with 'I' followed by uppercase letter
            if (name.length() >= 2 && name[0] == 'I' &&
                std::isupper(static_cast<unsigned char>(name[1])))
            {
                isInterface = true;
            }

            if (isInterface)
            {
                decl->interfaces.push_back(name);
            }
            else if (decl->baseClass.empty())
            {
                decl->baseClass = name;
            }
            else
            {
                // Multiple base classes not allowed, treat as interface
                decl->interfaces.push_back(name);
            }
        }

        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Parse class body
    Visibility currentVisibility = Visibility::Public;

    while (!check(TokenKind::KwEnd) && !check(TokenKind::Eof))
    {
        // Skip stray semicolons (can happen after error recovery)
        if (check(TokenKind::Semicolon))
        {
            advance();
            continue;
        }

        // Check for visibility specifier
        if (check(TokenKind::KwPrivate))
        {
            advance();
            currentVisibility = Visibility::Private;
            continue;
        }
        if (check(TokenKind::KwPublic))
        {
            advance();
            currentVisibility = Visibility::Public;
            continue;
        }

        // Parse member(s) - fields can have comma-separated names
        auto members = parseClassMembers(currentVisibility);
        for (auto &member : members)
        {
            decl->members.push_back(std::move(member));
        }
    }

    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    expect(TokenKind::Semicolon, "';'");

    return decl;
}

std::unique_ptr<Decl> Parser::parseInterface(const std::string &name, il::support::SourceLoc loc)
{
    auto decl = std::make_unique<InterfaceDecl>(name, loc);

    // Optional heritage clause: (Interface1, Interface2)
    if (match(TokenKind::LParen))
    {
        do
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected interface name");
                break;
            }
            decl->baseInterfaces.push_back(current_.text);
            advance();
        } while (match(TokenKind::Comma));

        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Parse interface methods
    while (!check(TokenKind::KwEnd) && !check(TokenKind::Eof))
    {
        auto methodLoc = current_.loc;
        MethodSig sig;
        sig.loc = methodLoc;

        if (check(TokenKind::KwProcedure))
        {
            advance();

            if (!check(TokenKind::Identifier))
            {
                error("expected method name");
                resyncAfterError();
                continue;
            }
            sig.name = current_.text;
            advance();

            // Parse parameters
            if (match(TokenKind::LParen))
            {
                if (!check(TokenKind::RParen))
                {
                    auto params = parseParameters();
                    for (auto &pd : params)
                    {
                        ParamDecl p;
                        p.name = std::move(pd.name);
                        p.type = std::move(pd.type);
                        p.isVar = pd.isVar;
                        p.isConst = pd.isConst;
                        p.loc = pd.loc;
                        sig.params.push_back(std::move(p));
                    }
                }
                expect(TokenKind::RParen, "')'");
            }

            expect(TokenKind::Semicolon, "';'");
        }
        else if (check(TokenKind::KwFunction))
        {
            advance();

            if (!check(TokenKind::Identifier))
            {
                error("expected method name");
                resyncAfterError();
                continue;
            }
            sig.name = current_.text;
            advance();

            // Parse parameters
            if (match(TokenKind::LParen))
            {
                if (!check(TokenKind::RParen))
                {
                    auto params = parseParameters();
                    for (auto &pd : params)
                    {
                        ParamDecl p;
                        p.name = std::move(pd.name);
                        p.type = std::move(pd.type);
                        p.isVar = pd.isVar;
                        p.isConst = pd.isConst;
                        p.loc = pd.loc;
                        sig.params.push_back(std::move(p));
                    }
                }
                expect(TokenKind::RParen, "')'");
            }

            // Return type
            if (!expect(TokenKind::Colon, "':'"))
            {
                resyncAfterError();
                continue;
            }
            sig.returnType = parseType();

            expect(TokenKind::Semicolon, "';'");
        }
        else
        {
            error("expected 'procedure' or 'function' in interface");
            resyncAfterError();
            continue;
        }

        decl->methods.push_back(std::move(sig));
    }

    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    expect(TokenKind::Semicolon, "';'");

    return decl;
}

std::vector<ClassMember> Parser::parseClassMembers(Visibility currentVisibility)
{
    std::vector<ClassMember> result;
    ClassMember member;
    member.visibility = currentVisibility;
    member.loc = current_.loc;

    // Property: property Name: Type read Getter [write Setter];
    if (check(TokenKind::KwProperty))
    {
        advance();

        // Name
        if (!check(TokenKind::Identifier))
        {
            error("expected property name");
            resyncAfterError();
            return result;
        }
        std::string propName = current_.text;
        advance();

        if (!expect(TokenKind::Colon, "':'"))
        {
            resyncAfterError();
            return result;
        }

        // Type
        auto typeNode = parseType();
        if (!typeNode)
        {
            resyncAfterError();
            return result;
        }

        // read
        if (!(check(TokenKind::Identifier) && current_.canonical == "read"))
        {
            error("expected 'read' in property");
            resyncAfterError();
            return result;
        }
        advance();
        if (!check(TokenKind::Identifier))
        {
            error("expected getter/field name after 'read'");
            resyncAfterError();
            return result;
        }
        std::string getter = current_.text;
        advance();

        // Optional write
        std::string setter;
        if (check(TokenKind::Identifier) && current_.canonical == "write")
        {
            advance();
            if (!check(TokenKind::Identifier))
            {
                error("expected setter/field name after 'write'");
                resyncAfterError();
                return result;
            }
            setter = current_.text;
            advance();
        }

        // Optional semicolon
        match(TokenKind::Semicolon);

        member.memberKind = ClassMember::Kind::Property;
        auto prop = std::make_unique<PropertyDecl>(propName, std::move(typeNode), member.loc);
        prop->getter = std::move(getter);
        prop->setter = std::move(setter);
        prop->visibility = currentVisibility;
        member.property = std::move(prop);
        result.push_back(std::move(member));
        return result;
    }

    // Constructor (signature only in class declaration)
    if (check(TokenKind::KwConstructor))
    {
        member.memberKind = ClassMember::Kind::Constructor;
        member.methodDecl = parseConstructorSignature();
        result.push_back(std::move(member));
        return result;
    }

    // Destructor (signature only in class declaration)
    if (check(TokenKind::KwDestructor))
    {
        member.memberKind = ClassMember::Kind::Destructor;
        member.methodDecl = parseDestructorSignature();
        result.push_back(std::move(member));
        return result;
    }

    // Procedure method (signature only in class)
    if (check(TokenKind::KwProcedure))
    {
        member.memberKind = ClassMember::Kind::Method;
        member.methodDecl = parseMethodSignature(/*isFunction=*/false);
        result.push_back(std::move(member));
        return result;
    }

    // Function method (signature only in class)
    if (check(TokenKind::KwFunction))
    {
        member.memberKind = ClassMember::Kind::Method;
        member.methodDecl = parseMethodSignature(/*isFunction=*/true);
        result.push_back(std::move(member));
        return result;
    }

    // Field: [weak] ident_list : type ;
    member.memberKind = ClassMember::Kind::Field;

    // Check for weak modifier
    bool isWeak = false;
    if (check(TokenKind::KwWeak))
    {
        isWeak = true;
        advance();
    }
    member.isWeak = isWeak;

    // Parse first field name
    if (!check(TokenKind::Identifier))
    {
        error("expected field name");
        resyncAfterError();
        result.push_back(std::move(member));
        return result;
    }

    // Collect all field names
    std::vector<std::string> fieldNames;
    fieldNames.push_back(current_.text);
    advance();

    // Handle comma-separated field names: x, y, z: Type;
    while (match(TokenKind::Comma))
    {
        if (check(TokenKind::Identifier))
        {
            fieldNames.push_back(current_.text);
            advance();
        }
    }

    // Expect ":"
    if (!expect(TokenKind::Colon, "':'"))
    {
        resyncAfterError();
        result.push_back(std::move(member));
        return result;
    }

    // Parse type (shared by all fields in this declaration)
    auto fieldType = parseType();

    // Expect ";"
    expect(TokenKind::Semicolon, "';'");

    // Create a ClassMember for each field name
    for (const auto &name : fieldNames)
    {
        ClassMember fieldMember;
        fieldMember.memberKind = ClassMember::Kind::Field;
        fieldMember.visibility = currentVisibility;
        fieldMember.loc = member.loc;
        fieldMember.isWeak = isWeak;
        fieldMember.fieldName = name;
        // Clone the type node for each field
        if (fieldType)
        {
            fieldMember.fieldType = fieldType->clone();
        }
        result.push_back(std::move(fieldMember));
    }

    return result;
}

std::unique_ptr<Decl> Parser::parseMethodSignature(bool isFunction)
{
    auto loc = current_.loc;

    // Consume procedure/function keyword
    if (isFunction)
    {
        if (!expect(TokenKind::KwFunction, "'function'"))
            return nullptr;
    }
    else
    {
        if (!expect(TokenKind::KwProcedure, "'procedure'"))
            return nullptr;
    }

    // Parse name
    if (!check(TokenKind::Identifier))
    {
        error(isFunction ? "expected function name" : "expected procedure name");
        return nullptr;
    }
    auto name = current_.text;
    advance();

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

    // For functions, parse return type
    std::unique_ptr<TypeNode> returnType;
    if (isFunction)
    {
        if (!expect(TokenKind::Colon, "':'"))
            return nullptr;
        returnType = parseType();
        if (!returnType)
            return nullptr;
    }

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    // Handle optional method modifiers (virtual, override)
    bool isVirtual = false;
    bool isOverride = false;
    bool isAbstract = false;
    while (check(TokenKind::KwVirtual) || check(TokenKind::KwOverride) || check(TokenKind::KwAbstract))
    {
        if (match(TokenKind::KwVirtual))
            isVirtual = true;
        if (match(TokenKind::KwOverride))
            isOverride = true;
        if (match(TokenKind::KwAbstract))
            isAbstract = true;
        // Expect ";" after modifier
        if (!expect(TokenKind::Semicolon, "';'"))
            return nullptr;
    }

    // Create the appropriate decl (signature only, no body)
    if (isFunction)
    {
        auto decl = std::make_unique<FunctionDecl>(std::move(name), std::move(params),
                                                   std::move(returnType), loc);
        decl->isForward = true; // Treat as forward declaration (no body)
        decl->isVirtual = isVirtual;
        decl->isOverride = isOverride;
        decl->isAbstract = isAbstract;
        return decl;
    }
    else
    {
        auto decl = std::make_unique<ProcedureDecl>(std::move(name), std::move(params), loc);
        decl->isForward = true; // Treat as forward declaration (no body)
        decl->isVirtual = isVirtual;
        decl->isOverride = isOverride;
        decl->isAbstract = isAbstract;
        return decl;
    }
}

std::unique_ptr<Decl> Parser::parseConstructor()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwConstructor, "'constructor'"))
        return nullptr;

    // Parse name - may be ClassName.MethodName for method implementations
    if (!check(TokenKind::Identifier))
    {
        error("expected constructor name");
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
            error("expected constructor name after '.'");
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

    auto decl = std::make_unique<ConstructorDecl>(std::move(name), std::move(params), loc);
    decl->className = std::move(className);

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

std::unique_ptr<Decl> Parser::parseDestructor()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwDestructor, "'destructor'"))
        return nullptr;

    // Parse name - may be ClassName.MethodName for method implementations
    if (!check(TokenKind::Identifier))
    {
        error("expected destructor name");
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
            error("expected destructor name after '.'");
            return nullptr;
        }
        name = current_.text;
        advance();
    }

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    auto decl = std::make_unique<DestructorDecl>(std::move(name), loc);
    decl->className = std::move(className);

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

std::unique_ptr<Decl> Parser::parseConstructorSignature()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwConstructor, "'constructor'"))
        return nullptr;

    // Parse name
    if (!check(TokenKind::Identifier))
    {
        error("expected constructor name");
        return nullptr;
    }
    auto name = current_.text;
    advance();

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

    auto decl = std::make_unique<ConstructorDecl>(std::move(name), std::move(params), loc);
    decl->isForward = true;  // Mark as signature only
    return decl;
}

std::unique_ptr<Decl> Parser::parseDestructorSignature()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwDestructor, "'destructor'"))
        return nullptr;

    // Parse name
    if (!check(TokenKind::Identifier))
    {
        error("expected destructor name");
        return nullptr;
    }
    auto name = current_.text;
    advance();

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    // Handle optional method modifiers (virtual, override)
    bool isVirtual = false;
    bool isOverride = false;
    while (check(TokenKind::KwVirtual) || check(TokenKind::KwOverride))
    {
        if (match(TokenKind::KwVirtual))
            isVirtual = true;
        if (match(TokenKind::KwOverride))
            isOverride = true;
        // Expect ";" after modifier
        if (!expect(TokenKind::Semicolon, "';'"))
            return nullptr;
    }

    auto decl = std::make_unique<DestructorDecl>(std::move(name), loc);
    decl->isForward = true;  // Mark as signature only
    decl->isVirtual = isVirtual;
    decl->isOverride = isOverride;
    return decl;
}

std::vector<std::string> Parser::parseIdentList()
{
    std::vector<std::string> names;

    if (!check(TokenKind::Identifier))
    {
        error("expected identifier");
        return names;
    }

    names.push_back(current_.text);
    advance();

    while (match(TokenKind::Comma))
    {
        if (!check(TokenKind::Identifier))
        {
            error("expected identifier after ','");
            break;
        }
        names.push_back(current_.text);
        advance();
    }

    return names;
}

//=============================================================================
// Program/Unit Parsing
//=============================================================================

} // namespace il::frontends::pascal

