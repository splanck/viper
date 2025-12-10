//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Parser_Type.cpp
// Purpose: Type parsing for Viper Pascal.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/AST.hpp"

namespace il::frontends::pascal
{


std::unique_ptr<TypeNode> Parser::parseType()
{
    auto type = parseBaseType();
    if (!type)
        return nullptr;

    // Check for optional suffix (?)
    if (match(TokenKind::Question))
    {
        auto loc = current_.loc;
        type = std::make_unique<OptionalTypeNode>(std::move(type), loc);

        // Reject T?? (double optional) - second ? could be another Question
        // or could be consumed as part of a ?? (NilCoalesce) token by the lexer
        if (check(TokenKind::Question) || check(TokenKind::NilCoalesce))
        {
            error("double optional type is not allowed");
            return nullptr;
        }
    }
    else if (check(TokenKind::NilCoalesce))
    {
        // T?? written without space - lexer tokenized as NilCoalesce
        error("double optional type is not allowed");
        return nullptr;
    }

    return type;
}

std::unique_ptr<TypeNode> Parser::parseBaseType()
{
    auto loc = current_.loc;

    // Array type
    if (check(TokenKind::KwArray))
    {
        return parseArrayType();
    }

    // Record type
    if (check(TokenKind::KwRecord))
    {
        return parseRecordType();
    }

    // Set type
    if (check(TokenKind::KwSet))
    {
        return parseSetType();
    }

    // Pointer type
    if (check(TokenKind::Caret))
    {
        return parsePointerType();
    }

    // Procedure type
    if (check(TokenKind::KwProcedure))
    {
        return parseProcedureType();
    }

    // Function type
    if (check(TokenKind::KwFunction))
    {
        return parseFunctionType();
    }

    // Enumeration type (starts with open paren containing identifiers)
    if (check(TokenKind::LParen))
    {
        return parseEnumType();
    }

    // Named type (identifier)
    if (check(TokenKind::Identifier))
    {
        auto name = current_.text;
        advance();
        return std::make_unique<NamedTypeNode>(std::move(name), loc);
    }

    error("expected type");
    return nullptr;
}

std::unique_ptr<TypeNode> Parser::parseArrayType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwArray, "'array'"))
        return nullptr;

    std::vector<ArrayTypeNode::DimSize> dimensions;

    // Check for dimension specification [...]
    if (match(TokenKind::LBracket))
    {
        // Parse dimensions (comma-separated)
        do
        {
            ArrayTypeNode::DimSize dim;

            // Parse dimension size expression
            auto expr = parseExpression();
            if (!expr)
                return nullptr;

            // Check for range (..) - not supported in v0.1 (0-based arrays only)
            if (check(TokenKind::DotDot))
            {
                error("range syntax 'low..high' is not supported; use single size "
                      "(e.g., array[10] of T for 0-based array)");
                return nullptr;
            }

            // Size expression - array bounds are 0..size-1
            dim.size = std::move(expr);

            dimensions.push_back(std::move(dim));
        } while (match(TokenKind::Comma));

        if (!expect(TokenKind::RBracket, "']'"))
            return nullptr;
    }

    // Expect "of"
    if (!expect(TokenKind::KwOf, "'of'"))
        return nullptr;

    // Parse element type
    auto elemType = parseType();
    if (!elemType)
        return nullptr;

    return std::make_unique<ArrayTypeNode>(std::move(dimensions), std::move(elemType), loc);
}

std::unique_ptr<TypeNode> Parser::parseRecordType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwRecord, "'record'"))
        return nullptr;

    std::vector<RecordField> fields;

    // Parse fields until "end"
    while (!check(TokenKind::KwEnd) && !check(TokenKind::Eof))
    {
        auto fieldLoc = current_.loc;

        // Parse field names
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

        // Parse field type
        auto fieldType = parseType();
        if (!fieldType)
        {
            resyncAfterError();
            continue;
        }

        // Create a field for each name
        for (size_t i = 0; i < names.size(); ++i)
        {
            RecordField field;
            field.name = std::move(names[i]);
            if (i + 1 < names.size())
            {
                // Clone type for all but last name
                // For simplicity, we re-parse - but this is a limitation
                // In practice, all fields share the same type semantically
                field.type = std::make_unique<NamedTypeNode>(
                    static_cast<NamedTypeNode *>(fieldType.get())->name, fieldLoc);
            }
            else
            {
                field.type = std::move(fieldType);
            }
            field.loc = fieldLoc;
            fields.push_back(std::move(field));
        }

        // Semicolon after field declaration
        match(TokenKind::Semicolon);
    }

    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    return std::make_unique<RecordTypeNode>(std::move(fields), loc);
}

std::unique_ptr<TypeNode> Parser::parseEnumType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::LParen, "'('"))
        return nullptr;

    std::vector<std::string> values;

    // Parse enum values
    // Note: In Pascal, enum values can be any identifier-like token,
    // including keywords like 'div', 'mod', etc.
    if (!check(TokenKind::RParen))
    {
        do
        {
            // Accept identifiers and keyword tokens as enum values
            if (check(TokenKind::Identifier) || isKeyword(current_.kind))
            {
                values.push_back(current_.text);
                advance();
            }
            else
            {
                error("expected enum value identifier");
                return nullptr;
            }
        } while (match(TokenKind::Comma));
    }

    if (!expect(TokenKind::RParen, "')'"))
        return nullptr;

    return std::make_unique<EnumTypeNode>(std::move(values), loc);
}

std::unique_ptr<TypeNode> Parser::parsePointerType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::Caret, "'^'"))
        return nullptr;

    auto pointeeType = parseType();
    if (!pointeeType)
        return nullptr;

    return std::make_unique<PointerTypeNode>(std::move(pointeeType), loc);
}

std::unique_ptr<TypeNode> Parser::parseSetType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwSet, "'set'"))
        return nullptr;

    if (!expect(TokenKind::KwOf, "'of'"))
        return nullptr;

    auto elemType = parseType();
    if (!elemType)
        return nullptr;

    return std::make_unique<SetTypeNode>(std::move(elemType), loc);
}

std::unique_ptr<TypeNode> Parser::parseProcedureType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwProcedure, "'procedure'"))
        return nullptr;

    std::vector<ParamSpec> params;

    // Optional parameter list
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            auto paramDecls = parseParameters();
            for (auto &pd : paramDecls)
            {
                ParamSpec ps;
                ps.name = std::move(pd.name);
                ps.type = std::move(pd.type);
                ps.isVar = pd.isVar;
                ps.isConst = pd.isConst;
                ps.loc = pd.loc;
                params.push_back(std::move(ps));
            }
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    return std::make_unique<ProcedureTypeNode>(std::move(params), loc);
}

std::unique_ptr<TypeNode> Parser::parseFunctionType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwFunction, "'function'"))
        return nullptr;

    std::vector<ParamSpec> params;

    // Optional parameter list
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            auto paramDecls = parseParameters();
            for (auto &pd : paramDecls)
            {
                ParamSpec ps;
                ps.name = std::move(pd.name);
                ps.type = std::move(pd.type);
                ps.isVar = pd.isVar;
                ps.isConst = pd.isConst;
                ps.loc = pd.loc;
                params.push_back(std::move(ps));
            }
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

    return std::make_unique<FunctionTypeNode>(std::move(params), std::move(returnType), loc);
}

//=============================================================================
// Declaration Parsing
//=============================================================================


} // namespace il::frontends::pascal

