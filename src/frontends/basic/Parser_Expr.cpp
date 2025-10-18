//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the Pratt parser responsible for BASIC expression parsing.  The
// entry points here are invoked by the statement parser whenever an expression
// production is required.  Operator precedence and associativity are encoded in
// the helper routines so the AST produced matches the surface language rules.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Parser.hpp"
#include "il/io/StringEscape.hpp"
#include <cstdio>
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
        case TokenKind::Caret:
            return 7;
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
        case TokenKind::KeywordAndAlso:
            return 2;
        case TokenKind::KeywordOr:
        case TokenKind::KeywordOrElse:
            return 1;
        default:
            return 0;
    }
}

/// @brief Parse an expression starting at the current token using Pratt parsing.
/// @details Implements the BASIC expression production by first parsing a unary operand and then
/// consuming infix operators in order of decreasing precedence. Diagnostics are emitted by helper
/// routines (for example, when sub-expressions are missing) while this function orchestrates the
/// climb.
/// @param min_prec Minimum precedence required to continue parsing infix operators.
/// @return Parsed expression subtree.
ExprPtr Parser::parseExpression(int min_prec)
{
    auto left = parseUnaryExpression();
    return parseInfixRhs(std::move(left), min_prec);
}

/// @brief Parse a unary expression or delegate to primary parsing when no prefix operator is
/// present.
/// @details Recognizes the grammar `unary := NOT unary | primary`. The helper consumes the NOT
/// keyword when present and recurses with the prefix precedence; otherwise it defers to
/// parsePrimary(). Any diagnostics originate from parseExpression or parsePrimary when required
/// operands are absent.
/// @return Expression node representing the parsed unary or primary expression.
ExprPtr Parser::parseUnaryExpression()
{
    if (at(TokenKind::KeywordNot) || at(TokenKind::Plus) || at(TokenKind::Minus))
    {
        auto tok = peek();
        consume();
        TokenKind precToken = tok.kind;
        if (tok.kind == TokenKind::KeywordNot)
            precToken = TokenKind::KeywordNot;
        auto operand = parseExpression(precedence(precToken));
        auto u = std::make_unique<UnaryExpr>();
        u->loc = tok.loc;
        switch (tok.kind)
        {
            case TokenKind::KeywordNot:
                u->op = UnaryExpr::Op::LogicalNot;
                break;
            case TokenKind::Plus:
                u->op = UnaryExpr::Op::Plus;
                break;
            case TokenKind::Minus:
                u->op = UnaryExpr::Op::Negate;
                break;
            default:
                u->op = UnaryExpr::Op::LogicalNot;
                break;
        }
        u->expr = std::move(operand);
        return u;
    }
    return parsePrimary();
}

/// @brief Parse the right-hand side of an infix expression chain.
/// @details Continues the Pratt parsing loop by consuming as many infix operators as bind tighter
/// than @p min_prec. Each operator pulls in a right-hand operand via parseExpression with a higher
/// minimum precedence, ensuring correct associativity. Missing operands propagate whatever result
/// parseExpression produces (typically a recovery literal).
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
        const bool rightAssociative = (op == TokenKind::Caret);
        auto right = parseExpression(rightAssociative ? prec : prec + 1);
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
            case TokenKind::Caret:
                bin->op = BinaryExpr::Op::Pow;
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
            case TokenKind::KeywordAndAlso:
                bin->op = BinaryExpr::Op::LogicalAndShort;
                break;
            case TokenKind::KeywordOrElse:
                bin->op = BinaryExpr::Op::LogicalOrShort;
                break;
            case TokenKind::KeywordAnd:
                bin->op = BinaryExpr::Op::LogicalAnd;
                break;
            case TokenKind::KeywordOr:
                bin->op = BinaryExpr::Op::LogicalOr;
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

/// @brief Parse a numeric literal expression from the current token.
/// @details Consumes a token classified as TokenKind::Number and constructs the corresponding BASIC
/// literal node. Presence of a decimal point, exponent, or type-marker suffix (%/&/!/ #) determines
/// whether an IntExpr or FloatExpr is emitted and records explicit suffix intent on the AST node.
/// The lexer guarantees the lexeme conforms to the numeric grammar, so no diagnostics are produced
/// here; the standard library conversion falls back to zero on malformed values, matching BASIC's
/// permissive semantics.
/// @return Newly allocated numeric literal expression.
ExprPtr Parser::parseNumber()
{
    auto loc = peek().loc;
    std::string lex = peek().lexeme;
    char suffix = '\0';
    if (!lex.empty())
    {
        char last = lex.back();
        switch (last)
        {
            case '#':
            case '!':
            case '%':
            case '&':
                suffix = last;
                lex.pop_back();
                break;
            default:
                break;
        }
    }

    const bool hasDot = lex.find('.') != std::string::npos;
    const bool hasExp = lex.find_first_of("Ee") != std::string::npos;
    const bool isFloatLiteral = hasDot || hasExp || suffix == '!' || suffix == '#';

    if (isFloatLiteral)
    {
        auto e = std::make_unique<FloatExpr>();
        e->loc = loc;
        e->value = std::strtod(lex.c_str(), nullptr);
        if (suffix == '!')
            e->suffix = FloatExpr::Suffix::Single;
        else if (suffix == '#')
            e->suffix = FloatExpr::Suffix::Double;
        consume();
        return e;
    }

    int64_t v = std::strtoll(lex.c_str(), nullptr, 10);
    auto e = std::make_unique<IntExpr>();
    e->loc = loc;
    e->value = v;
    if (suffix == '%')
        e->suffix = IntExpr::Suffix::Integer;
    else if (suffix == '&')
        e->suffix = IntExpr::Suffix::Long;
    consume();
    return e;
}

/// @brief Parse a string literal expression from the current token.
/// @details Implements the BASIC production `string-literal := "..."` by consuming the current
/// TokenKind::String token. Escape sequences such as `\n`, `\t`, `\"`, and `\\` are decoded here so
/// downstream passes observe the literal characters. Malformed escape sequences produce a
/// diagnostic when a @c DiagnosticEmitter is available.
/// @return Newly allocated string literal expression.
ExprPtr Parser::parseString()
{
    auto e = std::make_unique<StringExpr>();
    e->loc = peek().loc;
    std::string decoded;
    std::string err;
    if (!il::io::decodeEscapedString(peek().lexeme, decoded, &err))
    {
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B0003",
                           peek().loc,
                           static_cast<uint32_t>(peek().lexeme.size()),
                           err);
        }
        else
        {
            std::fprintf(stderr, "%s\n", err.c_str());
        }
        decoded = peek().lexeme;
    }
    e->value = std::move(decoded);
    consume();
    return e;
}

/// @brief Parse a call to a BASIC builtin function.
/// @details Implements `builtin-call := BUILTIN '(' [expr {',' expr}] ')'` where the argument
/// structure depends on @p builtin. Parentheses and separators are enforced via expect(), which
/// reports diagnostics when tokens are missing but still attempts recovery so parsing can
/// continue.
/// @param builtin Enumerated builtin resolved by lookupBuiltin().
/// @param loc Source location of the builtin identifier.
/// @return Newly allocated builtin call expression with parsed arguments.
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
             builtin == BuiltinCallExpr::Builtin::Int || builtin == BuiltinCallExpr::Builtin::Fix ||
             builtin == BuiltinCallExpr::Builtin::Round || builtin == BuiltinCallExpr::Builtin::Instr)
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

/// @brief Construct an AST node for a scalar variable reference.
/// @details Called after the identifier token has been consumed from the stream. No diagnostics are
/// emitted here; name resolution is deferred to later semantic stages.
/// @param name Identifier captured from the token stream.
/// @param loc Source location of the identifier.
/// @return Variable reference expression.
ExprPtr Parser::parseVariableRef(std::string name, il::support::SourceLoc loc)
{
    auto v = std::make_unique<VarExpr>();
    v->loc = loc;
    v->name = std::move(name);
    return v;
}

/// @brief Parse an array element reference of the form `name(expr)`.
/// @details After consuming the identifier, this helper expects an opening parenthesis, parses a
/// single index expression, and requires a closing parenthesis. The expect() calls emit diagnostics
/// on mismatched tokens but still advance so the parser can continue.
/// @param name Array identifier.
/// @param loc Source location of the identifier.
/// @return Array reference expression with the parsed index.
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

/// @brief Parse either an array reference, builtin call, user-defined call, or simple variable.
/// @details Implements the lookahead logic for the grammar fragment `identifier-suffix := '(' ...
/// ')' | ε`. If the identifier corresponds to a builtin function, control is delegated to
/// parseBuiltinCall(). Known arrays produce ArrayExpr nodes, while remaining identifiers with
/// parentheses become CallExpr invocations. When no parentheses are present, a VarExpr is emitted.
/// Errors encountered by expect() while parsing argument lists are reported before the helper
/// returns.
/// @return Expression node representing the chosen form.
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

/// @brief Parse a BASIC primary expression.
/// @details Covers literals, boolean keywords, builtin invocations, identifier references, and
/// parenthesized expressions per `primary := number | string | boolean | builtin-call |
/// identifier | '(' expression ')'`. When no valid production applies, the parser returns a zero
/// literal as error recovery; any diagnostics should already have been issued by the routines that
/// attempted to parse the unexpected token.
/// @return Parsed primary expression node, never null.
ExprPtr Parser::parsePrimary()
{
    ExprPtr expr;

    if (at(TokenKind::Number))
    {
        expr = parseNumber();
    }
    else if (at(TokenKind::String))
    {
        expr = parseString();
    }
    else if (at(TokenKind::KeywordTrue) || at(TokenKind::KeywordFalse))
    {
        auto b = std::make_unique<BoolExpr>();
        b->loc = peek().loc;
        b->value = at(TokenKind::KeywordTrue);
        consume();
        expr = std::move(b);
    }
#if VIPER_ENABLE_OOP
    else if (at(TokenKind::KeywordNew))
    {
        auto loc = peek().loc;
        consume(); // NEW

        std::string className;
        Token classTok = expect(TokenKind::Identifier);
        if (classTok.kind == TokenKind::Identifier)
            className = classTok.lexeme;

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

        auto newExpr = std::make_unique<NewExpr>();
        newExpr->loc = loc;
        newExpr->className = std::move(className);
        newExpr->args = std::move(args);
        expr = std::move(newExpr);
    }
    else if (at(TokenKind::KeywordMe))
    {
        auto me = std::make_unique<MeExpr>();
        me->loc = peek().loc;
        consume();
        expr = std::move(me);
    }
#endif
    else if (at(TokenKind::KeywordLbound))
    {
        auto loc = peek().loc;
        consume();
        expect(TokenKind::LParen);
        std::string name;
        Token ident = expect(TokenKind::Identifier);
        if (ident.kind == TokenKind::Identifier)
            name = ident.lexeme;
        expect(TokenKind::RParen);
        auto l = std::make_unique<LBoundExpr>();
        l->loc = loc;
        l->name = std::move(name);
        expr = std::move(l);
    }
    else if (at(TokenKind::KeywordUbound))
    {
        auto loc = peek().loc;
        consume();
        expect(TokenKind::LParen);
        std::string name;
        Token ident = expect(TokenKind::Identifier);
        if (ident.kind == TokenKind::Identifier)
            name = ident.lexeme;
        expect(TokenKind::RParen);
        auto u = std::make_unique<UBoundExpr>();
        u->loc = loc;
        u->name = std::move(name);
        expr = std::move(u);
    }
    else if (at(TokenKind::KeywordLof))
    {
        auto loc = peek().loc;
        consume();
        expect(TokenKind::LParen);
        expect(TokenKind::Hash);
        auto channel = parseExpression();
        expect(TokenKind::RParen);
        auto call = std::make_unique<BuiltinCallExpr>();
        call->loc = loc;
        call->Expr::loc = loc;
        call->builtin = BuiltinCallExpr::Builtin::Lof;
        call->args.push_back(std::move(channel));
        expr = std::move(call);
    }
    else if (at(TokenKind::KeywordEof))
    {
        auto loc = peek().loc;
        consume();
        expect(TokenKind::LParen);
        expect(TokenKind::Hash);
        auto channel = parseExpression();
        expect(TokenKind::RParen);
        auto call = std::make_unique<BuiltinCallExpr>();
        call->loc = loc;
        call->Expr::loc = loc;
        call->builtin = BuiltinCallExpr::Builtin::Eof;
        call->args.push_back(std::move(channel));
        expr = std::move(call);
    }
    else if (at(TokenKind::KeywordLoc))
    {
        auto loc = peek().loc;
        consume();
        expect(TokenKind::LParen);
        expect(TokenKind::Hash);
        auto channel = parseExpression();
        expect(TokenKind::RParen);
        auto call = std::make_unique<BuiltinCallExpr>();
        call->loc = loc;
        call->Expr::loc = loc;
        call->builtin = BuiltinCallExpr::Builtin::Loc;
        call->args.push_back(std::move(channel));
        expr = std::move(call);
    }
    else if (!at(TokenKind::Identifier))
    {
        if (auto b = lookupBuiltin(peek().lexeme))
        {
            auto loc = peek().loc;
            consume();
            expr = parseBuiltinCall(*b, loc);
        }
    }

    if (!expr && at(TokenKind::Identifier))
        expr = parseArrayOrVar();

    if (!expr && at(TokenKind::LParen))
    {
        consume();
        expr = parseExpression();
        expect(TokenKind::RParen);
    }

    if (!expr)
    {
        auto fallback = std::make_unique<IntExpr>();
        fallback->loc = peek().loc;
        fallback->value = 0;
        expr = std::move(fallback);
    }

#if VIPER_ENABLE_OOP
    while (expr && at(TokenKind::Dot))
    {
        consume();
        Token memberTok = expect(TokenKind::Identifier);
        std::string memberName;
        if (memberTok.kind == TokenKind::Identifier)
            memberName = memberTok.lexeme;
        auto memberLoc = memberTok.loc;

        if (at(TokenKind::LParen))
        {
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
            auto call = std::make_unique<MethodCallExpr>();
            call->loc = memberLoc;
            call->base = std::move(expr);
            call->method = std::move(memberName);
            call->args = std::move(args);
            expr = std::move(call);
        }
        else
        {
            auto access = std::make_unique<MemberAccessExpr>();
            access->loc = memberLoc;
            access->base = std::move(expr);
            access->member = std::move(memberName);
            expr = std::move(access);
        }
    }
#endif

    return expr;
}

} // namespace il::frontends::basic
