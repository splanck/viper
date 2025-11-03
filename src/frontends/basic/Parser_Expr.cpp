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
#include "viper/il/IO.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace il::frontends::basic
{
namespace
{
enum class Assoc
{
    Left,
    Right
};

struct PrefixParselet
{
    TokenKind kind;
    UnaryExpr::Op op;
    int rbp;
};

struct InfixParselet
{
    TokenKind kind;
    BinaryExpr::Op op;
    int lbp;
    Assoc assoc;
};

/// Pratt parsing relies on compact parselet tables that encode precedence and associativity.
/// Each entry corresponds to a BASIC operator and determines how expressions such as
/// `NOT A AND B` (prefix binds tighter than AND) and `A ^ B ^ C` (power is right associative)
/// are grouped without requiring large switch statements.
constexpr std::array<PrefixParselet, 3> prefixParselets{
    PrefixParselet{TokenKind::KeywordNot, UnaryExpr::Op::LogicalNot, 6},
    PrefixParselet{TokenKind::Plus, UnaryExpr::Op::Plus, 4},
    PrefixParselet{TokenKind::Minus, UnaryExpr::Op::Negate, 4},
};

constexpr std::array<InfixParselet, 17> infixParselets{
    InfixParselet{TokenKind::Caret, BinaryExpr::Op::Pow, 7, Assoc::Right},
    InfixParselet{TokenKind::Star, BinaryExpr::Op::Mul, 5, Assoc::Left},
    InfixParselet{TokenKind::Slash, BinaryExpr::Op::Div, 5, Assoc::Left},
    InfixParselet{TokenKind::Backslash, BinaryExpr::Op::IDiv, 5, Assoc::Left},
    InfixParselet{TokenKind::KeywordMod, BinaryExpr::Op::Mod, 5, Assoc::Left},
    InfixParselet{TokenKind::Plus, BinaryExpr::Op::Add, 4, Assoc::Left},
    InfixParselet{TokenKind::Minus, BinaryExpr::Op::Sub, 4, Assoc::Left},
    InfixParselet{TokenKind::Equal, BinaryExpr::Op::Eq, 3, Assoc::Left},
    InfixParselet{TokenKind::NotEqual, BinaryExpr::Op::Ne, 3, Assoc::Left},
    InfixParselet{TokenKind::Less, BinaryExpr::Op::Lt, 3, Assoc::Left},
    InfixParselet{TokenKind::LessEqual, BinaryExpr::Op::Le, 3, Assoc::Left},
    InfixParselet{TokenKind::Greater, BinaryExpr::Op::Gt, 3, Assoc::Left},
    InfixParselet{TokenKind::GreaterEqual, BinaryExpr::Op::Ge, 3, Assoc::Left},
    InfixParselet{TokenKind::KeywordAndAlso, BinaryExpr::Op::LogicalAndShort, 2, Assoc::Left},
    InfixParselet{TokenKind::KeywordOrElse, BinaryExpr::Op::LogicalOrShort, 1, Assoc::Left},
    InfixParselet{TokenKind::KeywordAnd, BinaryExpr::Op::LogicalAnd, 2, Assoc::Left},
    InfixParselet{TokenKind::KeywordOr, BinaryExpr::Op::LogicalOr, 1, Assoc::Left},
};

inline const PrefixParselet *findPrefix(TokenKind kind)
{
    const auto it =
        std::find_if(prefixParselets.begin(),
                     prefixParselets.end(),
                     [kind](const PrefixParselet &parselet) { return parselet.kind == kind; });
    return it == prefixParselets.end() ? nullptr : &*it;
}

inline const InfixParselet *findInfix(TokenKind kind)
{
    const auto it =
        std::find_if(infixParselets.begin(),
                     infixParselets.end(),
                     [kind](const InfixParselet &parselet) { return parselet.kind == kind; });
    return it == infixParselets.end() ? nullptr : &*it;
}

} // namespace

/// @brief Determine the binding power for an operator token during Pratt parsing.
/// @param k Token kind to inspect.
/// @return Numeric precedence; higher values bind more tightly, 0 for non-operators.
int Parser::precedence(TokenKind k)
{
    if (const auto *prefix = findPrefix(k))
        return prefix->rbp;
    if (const auto *infix = findInfix(k))
        return infix->lbp;
    return 0;
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
    return parseBinary(min_prec);
}

/// @brief Parse unary operators before delegating to primary expressions.
/// @return Parsed unary expression node.
ExprPtr Parser::parseUnary()
{
    const auto tok = peek();
    if (const auto *prefix = findPrefix(tok.kind))
    {
        consume();
        auto operand = parseBinary(prefix->rbp);
        auto expr = std::make_unique<UnaryExpr>();
        expr->loc = tok.loc;
        expr->op = prefix->op;
        expr->expr = std::move(operand);
        return expr;
    }

    auto primary = parsePrimary();
    return parsePostfix(std::move(primary));
}

/// @brief Parse infix operators using Pratt-style precedence climbing.
/// @param min_prec Minimum precedence required for an operator to bind.
/// @return Parsed expression node.
ExprPtr Parser::parseBinary(int min_prec)
{
    auto lhs = parseUnary();
    while (true)
    {
        const auto *parselet = findInfix(peek().kind);
        if (parselet == nullptr || parselet->lbp < min_prec)
            break;

        auto opTok = peek();
        consume();
        const int next_prec = parselet->assoc == Assoc::Right ? parselet->lbp : parselet->lbp + 1;
        auto rhs = parseBinary(next_prec);

        auto expr = std::make_unique<BinaryExpr>();
        expr->loc = opTok.loc;
        expr->op = parselet->op;
        expr->lhs = std::move(lhs);
        expr->rhs = std::move(rhs);
        lhs = std::move(expr);
    }
    return lhs;
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
             builtin == BuiltinCallExpr::Builtin::Round ||
             builtin == BuiltinCallExpr::Builtin::Instr)
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
    if (at(TokenKind::Number))
        return parseNumber();

    if (at(TokenKind::String))
        return parseString();

    if (at(TokenKind::KeywordTrue) || at(TokenKind::KeywordFalse))
    {
        auto boolean = std::make_unique<BoolExpr>();
        boolean->loc = peek().loc;
        boolean->value = at(TokenKind::KeywordTrue);
        consume();
        return boolean;
    }

    if (at(TokenKind::KeywordNew))
        return parseNewExpression();

    if (at(TokenKind::KeywordMe))
    {
        auto expr = std::make_unique<MeExpr>();
        expr->loc = peek().loc;
        consume();
        return expr;
    }

    if (at(TokenKind::KeywordLbound) || at(TokenKind::KeywordUbound))
        return parseBoundIntrinsic(peek().kind);

    if (at(TokenKind::KeywordLof) || at(TokenKind::KeywordEof) || at(TokenKind::KeywordLoc))
        return parseChannelIntrinsic(peek().kind);

    if (!at(TokenKind::Identifier))
    {
        if (auto builtin = lookupBuiltin(peek().lexeme))
        {
            auto loc = peek().loc;
            consume();
            return parseBuiltinCall(*builtin, loc);
        }
    }

    if (at(TokenKind::Identifier))
        return parseArrayOrVar();

    if (at(TokenKind::LParen))
    {
        consume();
        auto expr = parseExpression();
        expect(TokenKind::RParen);
        return expr;
    }

    auto fallback = std::make_unique<IntExpr>();
    fallback->loc = peek().loc;
    fallback->value = 0;
    return fallback;
}

/// @brief Parse a NEW expression allocating a class instance.
/// @return Newly allocated expression node.
ExprPtr Parser::parseNewExpression()
{
    auto loc = peek().loc;
    consume();

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
            if (!at(TokenKind::Comma))
                break;
            consume();
        }
    }
    expect(TokenKind::RParen);

    auto expr = std::make_unique<NewExpr>();
    expr->loc = loc;
    expr->className = std::move(className);
    expr->args = std::move(args);
    return expr;
}

/// @brief Parse LBOUND/UBOUND intrinsic expressions.
/// @param keyword Token identifying the intrinsic to parse.
/// @return Parsed intrinsic expression node.
ExprPtr Parser::parseBoundIntrinsic(TokenKind keyword)
{
    auto loc = peek().loc;
    consume();
    expect(TokenKind::LParen);
    std::string name;
    Token ident = expect(TokenKind::Identifier);
    if (ident.kind == TokenKind::Identifier)
        name = ident.lexeme;
    expect(TokenKind::RParen);

    if (keyword == TokenKind::KeywordLbound)
    {
        auto expr = std::make_unique<LBoundExpr>();
        expr->loc = loc;
        expr->name = std::move(name);
        return expr;
    }

    auto expr = std::make_unique<UBoundExpr>();
    expr->loc = loc;
    expr->name = std::move(name);
    return expr;
}

/// @brief Parse LOF/EOF/LOC intrinsic expressions operating on file channels.
/// @param keyword Token identifying the intrinsic to parse.
/// @return Parsed intrinsic expression node.
ExprPtr Parser::parseChannelIntrinsic(TokenKind keyword)
{
    auto loc = peek().loc;
    consume();
    expect(TokenKind::LParen);
    expect(TokenKind::Hash);
    auto channel = parseExpression();
    expect(TokenKind::RParen);

    auto expr = std::make_unique<BuiltinCallExpr>();
    expr->loc = loc;
    expr->Expr::loc = loc;
    switch (keyword)
    {
        case TokenKind::KeywordLof:
            expr->builtin = BuiltinCallExpr::Builtin::Lof;
            break;
        case TokenKind::KeywordEof:
            expr->builtin = BuiltinCallExpr::Builtin::Eof;
            break;
        default:
            expr->builtin = BuiltinCallExpr::Builtin::Loc;
            break;
    }
    expr->args.push_back(std::move(channel));
    return expr;
}

/// @brief Parse trailing member access or method call expressions.
/// @param expr Expression that may receive postfix operators.
/// @return Expression extended with postfix operations.
ExprPtr Parser::parsePostfix(ExprPtr expr)
{
    while (expr && at(TokenKind::Dot))
    {
        consume();
        Token ident = expect(TokenKind::Identifier);
        std::string member;
        if (ident.kind == TokenKind::Identifier)
            member = ident.lexeme;

        if (at(TokenKind::LParen))
        {
            expect(TokenKind::LParen);
            std::vector<ExprPtr> args;
            if (!at(TokenKind::RParen))
            {
                while (true)
                {
                    args.push_back(parseExpression());
                    if (!at(TokenKind::Comma))
                        break;
                    consume();
                }
            }
            expect(TokenKind::RParen);

            auto call = std::make_unique<MethodCallExpr>();
            call->loc = ident.loc;
            call->base = std::move(expr);
            call->method = std::move(member);
            call->args = std::move(args);
            expr = std::move(call);
            continue;
        }

        auto access = std::make_unique<MemberAccessExpr>();
        access->loc = ident.loc;
        access->base = std::move(expr);
        access->member = std::move(member);
        expr = std::move(access);
    }
    return expr;
}

} // namespace il::frontends::basic
