//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Parser_Type.cpp
/// @brief Type parsing implementation for the Zia parser.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Parser.hpp"

namespace il::frontends::zia
{

//===----------------------------------------------------------------------===//
// Type Parsing
//===----------------------------------------------------------------------===//

TypePtr Parser::parseType()
{
    TypePtr base = parseBaseType();
    if (!base)
        return nullptr;

    // Check for optional suffix ?
    while (match(TokenKind::Question))
    {
        base = std::make_unique<OptionalType>(base->loc, std::move(base));
    }

    return base;
}

TypePtr Parser::parseBaseType()
{
    // Named type (possibly qualified: Module.Type or Module.SubModule.Type)
    if (check(TokenKind::Identifier))
    {
        Token nameTok = advance();
        SourceLoc loc = nameTok.loc;
        std::string name = nameTok.text;

        // Handle qualified type names: Module.Type, Viper.Collections.List, etc.
        while (match(TokenKind::Dot))
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected identifier after '.' in qualified type name");
                return nullptr;
            }
            Token nextTok = advance();
            name += ".";
            name += nextTok.text;
        }

        // Check for fixed-size array (T[N]) or generic parameters (T[Type, ...])
        if (check(TokenKind::LBracket))
        {
            // Peek past '[' to see if the next token is an integer literal.
            // If so, parse as a fixed-size array type: T[N].
            // Otherwise fall through to the generic type path: T[Type, ...].
            if (peek(1).kind == TokenKind::IntegerLiteral)
            {
                advance(); // consume '['
                int64_t count = std::stoll(peek().text);
                if (count < 0)
                {
                    error("fixed-size array count must be non-negative");
                    return nullptr;
                }
                advance(); // consume N
                if (!expect(TokenKind::RBracket, "]"))
                    return nullptr;
                auto elemType = std::make_unique<NamedType>(loc, std::move(name));
                return std::make_unique<FixedArrayType>(
                    loc, std::move(elemType), static_cast<size_t>(count));
            }

            advance(); // consume '['
            std::vector<TypePtr> args;

            do
            {
                TypePtr arg = parseType();
                if (!arg)
                    return nullptr;
                args.push_back(std::move(arg));
            } while (match(TokenKind::Comma));

            if (!expect(TokenKind::RBracket, "]"))
                return nullptr;

            return std::make_unique<GenericType>(loc, std::move(name), std::move(args));
        }

        return std::make_unique<NamedType>(loc, std::move(name));
    }

    // Tuple or function type: (A, B) or (A, B) -> C
    Token lparenTok;
    if (match(TokenKind::LParen, &lparenTok))
    {
        SourceLoc loc = lparenTok.loc;
        std::vector<TypePtr> elements;

        if (!check(TokenKind::RParen))
        {
            do
            {
                TypePtr elem = parseType();
                if (!elem)
                    return nullptr;
                elements.push_back(std::move(elem));
            } while (match(TokenKind::Comma));
        }

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        // Check for function type
        if (match(TokenKind::Arrow))
        {
            TypePtr returnType = parseType();
            if (!returnType)
                return nullptr;

            return std::make_unique<FunctionType>(loc, std::move(elements), std::move(returnType));
        }

        // Tuple type
        return std::make_unique<TupleType>(loc, std::move(elements));
    }

    error("expected type");
    return nullptr;
}

} // namespace il::frontends::zia
