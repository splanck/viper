// File: src/frontends/basic/Parser_Expr.cpp
// Purpose: Expression parsing for BASIC (Pratt parser).
// Key invariants: Respects operator precedence for correct AST construction.
// Ownership/Lifetime: Expressions owned by caller via std::unique_ptr.
// Links: docs/class-catalog.md

#include "frontends/basic/Parser.hpp"
#include <cstdlib>

namespace il::frontends::basic
{

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

ExprPtr Parser::parseExpression(int min_prec)
{
    ExprPtr left;
    if (at(TokenKind::KeywordNot))
    {
        auto loc = peek().loc;
        consume();
        auto operand = parseExpression(precedence(TokenKind::KeywordNot));
        auto u = std::make_unique<UnaryExpr>();
        u->loc = loc;
        u->op = UnaryExpr::Op::Not;
        u->expr = std::move(operand);
        left = std::move(u);
    }
    else
    {
        left = parsePrimary();
    }
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

ExprPtr Parser::parsePrimary()
{
    if (at(TokenKind::Number))
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
        int v = std::atoi(lex.c_str());
        auto e = std::make_unique<IntExpr>();
        e->loc = loc;
        e->value = v;
        consume();
        return e;
    }
    if (at(TokenKind::String))
    {
        auto e = std::make_unique<StringExpr>();
        e->loc = peek().loc;
        e->value = peek().lexeme;
        consume();
        return e;
    }
    if (at(TokenKind::KeywordSqr) || at(TokenKind::KeywordAbs) || at(TokenKind::KeywordFloor) ||
        at(TokenKind::KeywordCeil) || at(TokenKind::KeywordSin) || at(TokenKind::KeywordCos) ||
        at(TokenKind::KeywordPow))
    {
        TokenKind tk = peek().kind;
        auto loc = peek().loc;
        consume();
        expect(TokenKind::LParen);
        auto arg = parseExpression();
        ExprPtr arg2;
        if (tk == TokenKind::KeywordPow)
        {
            expect(TokenKind::Comma);
            arg2 = parseExpression();
        }
        expect(TokenKind::RParen);
        auto call = std::make_unique<BuiltinCallExpr>();
        call->loc = loc;
        call->Expr::loc = loc;
        if (tk == TokenKind::KeywordSqr)
            call->builtin = BuiltinCallExpr::Builtin::Sqr;
        else if (tk == TokenKind::KeywordAbs)
            call->builtin = BuiltinCallExpr::Builtin::Abs;
        else if (tk == TokenKind::KeywordFloor)
            call->builtin = BuiltinCallExpr::Builtin::Floor;
        else if (tk == TokenKind::KeywordCeil)
            call->builtin = BuiltinCallExpr::Builtin::Ceil;
        else if (tk == TokenKind::KeywordSin)
            call->builtin = BuiltinCallExpr::Builtin::Sin;
        else if (tk == TokenKind::KeywordCos)
            call->builtin = BuiltinCallExpr::Builtin::Cos;
        else
            call->builtin = BuiltinCallExpr::Builtin::Pow;
        call->args.push_back(std::move(arg));
        if (tk == TokenKind::KeywordPow)
            call->args.push_back(std::move(arg2));
        return call;
    }
    if (at(TokenKind::KeywordRnd))
    {
        auto loc = peek().loc;
        consume();
        expect(TokenKind::LParen);
        expect(TokenKind::RParen);
        auto call = std::make_unique<BuiltinCallExpr>();
        call->loc = loc;
        call->Expr::loc = loc;
        call->builtin = BuiltinCallExpr::Builtin::Rnd;
        return call;
    }
    if (at(TokenKind::Identifier))
    {
        std::string name = peek().lexeme;
        auto loc = peek().loc;
        consume();
        if (at(TokenKind::LParen))
        {
            consume();
            if (name == "LEN" || name == "MID$" || name == "LEFT$" || name == "RIGHT$" ||
                name == "STR$" || name == "VAL" || name == "INT")
            {
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
                auto call = std::make_unique<BuiltinCallExpr>();
                call->loc = loc;
                call->Expr::loc = loc;
                if (name == "LEN")
                    call->builtin = BuiltinCallExpr::Builtin::Len;
                else if (name == "MID$")
                    call->builtin = BuiltinCallExpr::Builtin::Mid;
                else if (name == "LEFT$")
                    call->builtin = BuiltinCallExpr::Builtin::Left;
                else if (name == "RIGHT$")
                    call->builtin = BuiltinCallExpr::Builtin::Right;
                else if (name == "STR$")
                    call->builtin = BuiltinCallExpr::Builtin::Str;
                else if (name == "VAL")
                    call->builtin = BuiltinCallExpr::Builtin::Val;
                else
                    call->builtin = BuiltinCallExpr::Builtin::Int;
                call->args = std::move(args);
                return call;
            }
            else if (arrays_.count(name))
            {
                auto first = parseExpression();
                if (at(TokenKind::RParen))
                {
                    consume();
                    auto arr = std::make_unique<ArrayExpr>();
                    arr->loc = loc;
                    arr->name = name;
                    arr->index = std::move(first);
                    return arr;
                }
                // treat as call if more arguments follow
                std::vector<ExprPtr> args;
                args.push_back(std::move(first));
                while (true)
                {
                    expect(TokenKind::Comma);
                    args.push_back(parseExpression());
                    if (!at(TokenKind::Comma))
                        break;
                }
                expect(TokenKind::RParen);
                auto call = std::make_unique<CallExpr>();
                call->loc = loc;
                call->Expr::loc = loc;
                call->callee = name;
                call->args = std::move(args);
                return call;
            }
            else
            {
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
        }
        auto v = std::make_unique<VarExpr>();
        v->loc = loc;
        v->name = name;
        return v;
    }
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
