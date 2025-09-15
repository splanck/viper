// File: src/frontends/basic/Parser_Expr.cpp
// Purpose: Expression parsing for BASIC (Pratt parser).
// Key invariants: Respects operator precedence for correct AST construction.
// Ownership/Lifetime: Expressions owned by caller via std::unique_ptr.
// Links: docs/class-catalog.md

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Parser.hpp"
#include <cstdlib>

namespace il::frontends::basic
{
/// @brief Determine the binding power for an operator token during Pratt parsing.
/// @param k Token kind to inspect.
/// @return Numeric precedence; higher values bind more tightly, 0 for non-operators.
int Parser::precedence(TokenKind k)
{
    switch (k)
    {
        case TokenKind::KeywordNot:
            return 6;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Backslash:
        case TokenKind::KeywordMod:
            return 5;
        case TokenKind::Plus:
        case TokenKind::Minus:
            return 4;
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return 3;
        case TokenKind::KeywordAnd:
            return 2;
        case TokenKind::KeywordOr:
            return 1;
        default:
            return 0;
    }
}

/// @brief Parse an expression starting at the current token using Pratt parsing.
/// @param min_prec Minimum precedence required to continue parsing infix operators.
/// @return Parsed expression subtree.
ExprPtr Parser::parseExpression(int min_prec)
{
    auto left = parseUnaryExpression();
    return parseInfixRhs(std::move(left), min_prec);
}

/// @brief Parse a unary expression or delegate to primary parsing when no prefix operator is
/// present.
/// @return Expression node representing the parsed unary or primary expression.
ExprPtr Parser::parseUnaryExpression()
{
    if (at(TokenKind::KeywordNot))
    {
        auto loc = peek().loc;
        consume();
        auto operand = parseExpression(precedence(TokenKind::KeywordNot));
        auto u = std::make_unique<UnaryExpr>();
        u->loc = loc;
        u->op = UnaryExpr::Op::Not;
        u->expr = std::move(operand);
        return u;
    }
    return parsePrimary();
}

/// @brief Parse the right-hand side of an infix expression chain.
/// @param left Expression already parsed as the left operand.
/// @param min_prec Minimum precedence required for an operator to bind.
/// @return Combined expression tree including any parsed infix operations.
ExprPtr Parser::parseInfixRhs(ExprPtr left, int min_prec)
{
    while (true)
    {
        int prec = precedence(peek().kind);
        if (prec < min_prec || prec == 0)
            break;
        TokenKind op = peek().kind;
        auto opLoc = peek().loc;
        consume();
        auto right = parseExpression(prec + 1);
        auto bin = std::make_unique<BinaryExpr>();
        bin->loc = opLoc;
        switch (op)
        {
            case TokenKind::Plus:
                bin->op = BinaryExpr::Op::Add;
                break;
            case TokenKind::Minus:
                bin->op = BinaryExpr::Op::Sub;
                break;
            case TokenKind::Star:
                bin->op = BinaryExpr::Op::Mul;
                break;
            case TokenKind::Slash:
                bin->op = BinaryExpr::Op::Div;
                break;
            case TokenKind::Backslash:
                bin->op = BinaryExpr::Op::IDiv;
                break;
            case TokenKind::KeywordMod:
                bin->op = BinaryExpr::Op::Mod;
                break;
            case TokenKind::Equal:
                bin->op = BinaryExpr::Op::Eq;
                break;
            case TokenKind::NotEqual:
                bin->op = BinaryExpr::Op::Ne;
                break;
            case TokenKind::Less:
                bin->op = BinaryExpr::Op::Lt;
                break;
            case TokenKind::LessEqual:
                bin->op = BinaryExpr::Op::Le;
                break;
            case TokenKind::Greater:
                bin->op = BinaryExpr::Op::Gt;
                break;
            case TokenKind::GreaterEqual:
                bin->op = BinaryExpr::Op::Ge;
                break;
            case TokenKind::KeywordAnd:
                bin->op = BinaryExpr::Op::And;
                break;
            case TokenKind::KeywordOr:
                bin->op = BinaryExpr::Op::Or;
                break;
            default:
                bin->op = BinaryExpr::Op::Add;
        }
        bin->lhs = std::move(left);
        bin->rhs = std::move(right);
        left = std::move(bin);
    }
    return left;
}

ExprPtr Parser::parseNumber()
{
    auto loc = peek().loc;
    std::string lex = peek().lexeme;
    if (lex.find_first_of(".Ee#") != std::string::npos)
    {
        auto e = std::make_unique<FloatExpr>();
        e->loc = loc;
        e->value = std::strtod(lex.c_str(), nullptr);
        consume();
        return e;
    }
    int64_t v = std::strtoll(lex.c_str(), nullptr, 10);
    auto e = std::make_unique<IntExpr>();
    e->loc = loc;
    e->value = v;
    consume();
    return e;
}

ExprPtr Parser::parseString()
{
    auto e = std::make_unique<StringExpr>();
    e->loc = peek().loc;
    e->value = peek().lexeme;
    consume();
    return e;
}

ExprPtr Parser::parseBuiltinCall(BuiltinCallExpr::Builtin builtin, il::support::SourceLoc loc)
{
    expect(TokenKind::LParen);
    std::vector<ExprPtr> args;
    if (builtin == BuiltinCallExpr::Builtin::Rnd)
    {
        expect(TokenKind::RParen);
    }
    else if (builtin == BuiltinCallExpr::Builtin::Pow)
    {
        auto first = parseExpression();
        expect(TokenKind::Comma);
        auto second = parseExpression();
        expect(TokenKind::RParen);
        args.push_back(std::move(first));
        args.push_back(std::move(second));
    }
    else if (builtin == BuiltinCallExpr::Builtin::Len || builtin == BuiltinCallExpr::Builtin::Mid ||
             builtin == BuiltinCallExpr::Builtin::Left ||
             builtin == BuiltinCallExpr::Builtin::Right ||
             builtin == BuiltinCallExpr::Builtin::Str || builtin == BuiltinCallExpr::Builtin::Val ||
             builtin == BuiltinCallExpr::Builtin::Int || builtin == BuiltinCallExpr::Builtin::Instr)
    {
        if (!at(TokenKind::RParen))
        {
            while (true)
            {
                args.push_back(parseExpression());
                if (at(TokenKind::Comma))
                {
                    consume();
                    continue;
                }
                break;
            }
        }
        expect(TokenKind::RParen);
    }
    else
    {
        args.push_back(parseExpression());
        expect(TokenKind::RParen);
    }
    auto call = std::make_unique<BuiltinCallExpr>();
    call->loc = loc;
    call->Expr::loc = loc;
    call->builtin = builtin;
    call->args = std::move(args);
    return call;
}

ExprPtr Parser::parseVariableRef(std::string name, il::support::SourceLoc loc)
{
    auto v = std::make_unique<VarExpr>();
    v->loc = loc;
    v->name = std::move(name);
    return v;
}

ExprPtr Parser::parseArrayRef(std::string name, il::support::SourceLoc loc)
{
    expect(TokenKind::LParen);
    auto index = parseExpression();
    expect(TokenKind::RParen);
    auto arr = std::make_unique<ArrayExpr>();
    arr->loc = loc;
    arr->name = std::move(name);
    arr->index = std::move(index);
    return arr;
}

ExprPtr Parser::parseArrayOrVar()
{
    std::string name = peek().lexeme;
    auto loc = peek().loc;
    consume();
    if (at(TokenKind::LParen))
    {
        if (auto b = lookupBuiltin(name))
            return parseBuiltinCall(*b, loc);

        if (arrays_.count(name))
            return parseArrayRef(name, loc);

        expect(TokenKind::LParen);
        std::vector<ExprPtr> args;
        if (!at(TokenKind::RParen))
        {
            while (true)
            {
                args.push_back(parseExpression());
                if (at(TokenKind::Comma))
                {
                    consume();
                    continue;
                }
                break;
            }
        }
        expect(TokenKind::RParen);
        auto call = std::make_unique<CallExpr>();
        call->loc = loc;
        call->Expr::loc = loc;
        call->callee = name;
        call->args = std::move(args);
        return call;
    }
    return parseVariableRef(name, loc);
}

ExprPtr Parser::parsePrimary()
{
    if (at(TokenKind::Number))
        return parseNumber();
    if (at(TokenKind::String))
        return parseString();
    if (!at(TokenKind::Identifier))
    {
        if (auto b = lookupBuiltin(peek().lexeme))
        {
            auto loc = peek().loc;
            consume();
            return parseBuiltinCall(*b, loc);
        }
    }
    if (at(TokenKind::Identifier))
        return parseArrayOrVar();
    if (at(TokenKind::LParen))
    {
        consume();
        auto e = parseExpression();
        expect(TokenKind::RParen);
        return e;
    }
    auto e = std::make_unique<IntExpr>();
    e->loc = peek().loc;
    e->value = 0;
    return e;
}

} // namespace il::frontends::basic
