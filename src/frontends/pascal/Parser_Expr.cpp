//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Parser_Expr.cpp
// Purpose: Expression parsing for Viper Pascal.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Parser.hpp"

namespace il::frontends::pascal
{


std::unique_ptr<Expr> Parser::parseExpression()
{
    return parseCoalesce();
}

// Coalesce: relation { "??" relation } (left-associative, lowest precedence)
std::unique_ptr<Expr> Parser::parseCoalesce()
{
    auto left = parseRelation();
    if (!left)
        return nullptr;

    while (match(TokenKind::NilCoalesce))
    {
        auto loc = current_.loc;
        auto right = parseRelation();
        if (!right)
            return nullptr;
        left = std::make_unique<BinaryExpr>(
            BinaryExpr::Op::Coalesce, std::move(left), std::move(right), loc);
    }

    return left;
}

// Relation: simple [relop simple] | simple 'is' type
std::unique_ptr<Expr> Parser::parseRelation()
{
    auto left = parseSimple();
    if (!left)
        return nullptr;

    // Type-check operator: expr is T
    if (check(TokenKind::KwIs))
    {
        auto loc = current_.loc;
        advance(); // consume 'is'
        // Parse a type name after 'is'
        auto type = parseType();
        if (!type)
            return nullptr;
        return std::make_unique<IsExpr>(std::move(left), std::move(type), loc);
    }

    // Check for relational operators
    BinaryExpr::Op op;
    bool hasOp = false;

    if (check(TokenKind::Equal))
    {
        op = BinaryExpr::Op::Eq;
        hasOp = true;
    }
    else if (check(TokenKind::NotEqual))
    {
        op = BinaryExpr::Op::Ne;
        hasOp = true;
    }
    else if (check(TokenKind::Less))
    {
        op = BinaryExpr::Op::Lt;
        hasOp = true;
    }
    else if (check(TokenKind::Greater))
    {
        op = BinaryExpr::Op::Gt;
        hasOp = true;
    }
    else if (check(TokenKind::LessEqual))
    {
        op = BinaryExpr::Op::Le;
        hasOp = true;
    }
    else if (check(TokenKind::GreaterEqual))
    {
        op = BinaryExpr::Op::Ge;
        hasOp = true;
    }
    else if (check(TokenKind::KwIn))
    {
        op = BinaryExpr::Op::In;
        hasOp = true;
    }

    if (hasOp)
    {
        auto loc = current_.loc;
        advance();
        auto right = parseSimple();
        if (!right)
            return nullptr;
        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), loc);
    }

    return left;
}

// Simple: ["-" | "+"] term { addop term }
std::unique_ptr<Expr> Parser::parseSimple()
{
    // Handle leading unary +/-
    // Note: unary - applies only to the immediately following factor, not term
    // So "-x * y" should parse as "(-x) * y"
    // This is handled in parseFactor() for not, and here for +/-

    std::unique_ptr<Expr> left = parseTerm();
    if (!left)
        return nullptr;

    // Handle additive operators (including or)
    while (true)
    {
        BinaryExpr::Op op;
        if (check(TokenKind::Plus))
        {
            op = BinaryExpr::Op::Add;
        }
        else if (check(TokenKind::Minus))
        {
            op = BinaryExpr::Op::Sub;
        }
        else if (check(TokenKind::KwOr))
        {
            op = BinaryExpr::Op::Or;
        }
        else
        {
            break;
        }

        auto loc = current_.loc;
        advance();
        auto right = parseTerm();
        if (!right)
            return nullptr;
        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), loc);
    }

    return left;
}

// Term: factor { mulop factor }
std::unique_ptr<Expr> Parser::parseTerm()
{
    auto left = parseFactor();
    if (!left)
        return nullptr;

    while (true)
    {
        BinaryExpr::Op op;
        if (check(TokenKind::Star))
        {
            op = BinaryExpr::Op::Mul;
        }
        else if (check(TokenKind::Slash))
        {
            op = BinaryExpr::Op::Div;
        }
        else if (check(TokenKind::KwDiv))
        {
            op = BinaryExpr::Op::IntDiv;
        }
        else if (check(TokenKind::KwMod))
        {
            op = BinaryExpr::Op::Mod;
        }
        else if (check(TokenKind::KwAnd))
        {
            op = BinaryExpr::Op::And;
        }
        else
        {
            break;
        }

        auto loc = current_.loc;
        advance();
        auto right = parseFactor();
        if (!right)
            return nullptr;
        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), loc);
    }

    return left;
}

// Factor: "not" factor | ["+"|"-"] factor | primary
std::unique_ptr<Expr> Parser::parseFactor()
{
    // Handle unary operators: not, +, -
    if (check(TokenKind::KwNot))
    {
        auto loc = current_.loc;
        advance();
        auto operand = parseFactor();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Not, std::move(operand), loc);
    }

    if (check(TokenKind::Minus))
    {
        auto loc = current_.loc;
        advance();
        auto operand = parseFactor();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Neg, std::move(operand), loc);
    }

    if (check(TokenKind::Plus))
    {
        auto loc = current_.loc;
        advance();
        auto operand = parseFactor();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Plus, std::move(operand), loc);
    }

    return parsePrimary();
}

// Primary: literal | designator | "(" expr ")" | "@" factor | set-constructor
std::unique_ptr<Expr> Parser::parsePrimary()
{
    auto loc = current_.loc;

    // Integer literal
    if (check(TokenKind::IntegerLiteral))
    {
        auto value = current_.intValue;
        advance();
        return std::make_unique<IntLiteralExpr>(value, loc);
    }

    // Real literal
    if (check(TokenKind::RealLiteral))
    {
        auto value = current_.realValue;
        advance();
        return std::make_unique<RealLiteralExpr>(value, loc);
    }

    // String literal
    if (check(TokenKind::StringLiteral))
    {
        auto value = current_.canonical; // canonical contains the actual string content
        advance();
        return std::make_unique<StringLiteralExpr>(std::move(value), loc);
    }

    // Nil literal
    if (check(TokenKind::KwNil))
    {
        advance();
        return std::make_unique<NilLiteralExpr>(loc);
    }

    // Boolean literals (True/False are predefined identifiers)
    if (check(TokenKind::Identifier) && current_.isPredefined)
    {
        if (current_.canonical == "true")
        {
            advance();
            return std::make_unique<BoolLiteralExpr>(true, loc);
        }
        if (current_.canonical == "false")
        {
            advance();
            return std::make_unique<BoolLiteralExpr>(false, loc);
        }
    }

    // Identifier (designator)
    if (check(TokenKind::Identifier))
    {
        return parseDesignator();
    }

    // Parenthesized expression
    if (check(TokenKind::LParen))
    {
        advance();
        auto expr = parseExpression();
        if (!expr)
            return nullptr;
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
        return expr;
    }

    // Address-of operator
    if (check(TokenKind::At))
    {
        advance();
        auto operand = parseFactor();
        if (!operand)
            return nullptr;
        return std::make_unique<AddressOfExpr>(std::move(operand), loc);
    }

    // Set constructor [...]
    if (check(TokenKind::LBracket))
    {
        advance();
        std::vector<SetConstructorExpr::Element> elements;

        if (!check(TokenKind::RBracket))
        {
            do
            {
                auto start = parseExpression();
                if (!start)
                    return nullptr;

                SetConstructorExpr::Element elem;
                elem.start = std::move(start);

                // Check for range ..
                if (match(TokenKind::DotDot))
                {
                    elem.end = parseExpression();
                    if (!elem.end)
                        return nullptr;
                }

                elements.push_back(std::move(elem));
            } while (match(TokenKind::Comma));
        }

        if (!expect(TokenKind::RBracket, "']'"))
            return nullptr;

        return std::make_unique<SetConstructorExpr>(std::move(elements), loc);
    }

    error("expected expression");
    return nullptr;
}

// Designator: identifier { "." ident | "[" exprList "]" | "(" argList ")" | "^" }
std::unique_ptr<Expr> Parser::parseDesignator()
{
    auto loc = current_.loc;

    if (!check(TokenKind::Identifier))
    {
        error("expected identifier");
        return nullptr;
    }

    auto name = current_.text;
    advance();

    std::unique_ptr<Expr> base = std::make_unique<NameExpr>(std::move(name), loc);

    return parseDesignatorSuffix(std::move(base));
}

std::unique_ptr<Expr> Parser::parseDesignatorSuffix(std::unique_ptr<Expr> base)
{
    while (true)
    {
        auto loc = current_.loc;

        // Field access: .ident
        if (match(TokenKind::Dot))
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected field name after '.'");
                return nullptr;
            }
            auto field = current_.text;
            advance();
            base = std::make_unique<FieldExpr>(std::move(base), std::move(field), loc);
            continue;
        }

        // Index access: [exprList]
        if (match(TokenKind::LBracket))
        {
            auto indices = parseExprList();
            if (indices.empty() && hasError_)
                return nullptr;
            if (!expect(TokenKind::RBracket, "']'"))
                return nullptr;
            base = std::make_unique<IndexExpr>(std::move(base), std::move(indices), loc);
            continue;
        }

        // Call: (argList)
        if (match(TokenKind::LParen))
        {
            std::vector<std::unique_ptr<Expr>> args;
            if (!check(TokenKind::RParen))
            {
                args = parseExprList();
                if (args.empty() && hasError_)
                    return nullptr;
            }
            if (!expect(TokenKind::RParen, "')'"))
                return nullptr;
            base = std::make_unique<CallExpr>(std::move(base), std::move(args), loc);
            continue;
        }

        // Dereference: ^
        if (match(TokenKind::Caret))
        {
            base = std::make_unique<DereferenceExpr>(std::move(base), loc);
            continue;
        }

        // No more suffixes
        break;
    }

    return base;
}

std::vector<std::unique_ptr<Expr>> Parser::parseExprList()
{
    std::vector<std::unique_ptr<Expr>> result;

    auto expr = parseExpression();
    if (!expr)
        return result;
    result.push_back(std::move(expr));

    while (match(TokenKind::Comma))
    {
        expr = parseExpression();
        if (!expr)
            return result;
        result.push_back(std::move(expr));
    }

    return result;
}

//=============================================================================
// Statement Parsing
//=============================================================================


} // namespace il::frontends::pascal
