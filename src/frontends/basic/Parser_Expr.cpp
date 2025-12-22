//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "viper/il/IO.hpp"
#include <algorithm>
#include <array>
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

constexpr std::array<InfixParselet, 18> infixParselets{
    InfixParselet{TokenKind::Caret, BinaryExpr::Op::Pow, 7, Assoc::Right},
    InfixParselet{TokenKind::Star, BinaryExpr::Op::Mul, 5, Assoc::Left},
    InfixParselet{TokenKind::Slash, BinaryExpr::Op::Div, 5, Assoc::Left},
    InfixParselet{TokenKind::Backslash, BinaryExpr::Op::IDiv, 5, Assoc::Left},
    InfixParselet{TokenKind::KeywordMod, BinaryExpr::Op::Mod, 5, Assoc::Left},
    InfixParselet{TokenKind::Plus, BinaryExpr::Op::Add, 4, Assoc::Left},
    InfixParselet{TokenKind::Minus, BinaryExpr::Op::Sub, 4, Assoc::Left},
    InfixParselet{TokenKind::Ampersand, BinaryExpr::Op::Add, 4, Assoc::Left},
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
        // Handle IS and AS type operators (precedence 3, same as comparisons)
        if (peek().kind == TokenKind::KeywordIs && min_prec <= 3)
        {
            auto opTok = peek();
            consume(); // IS
            auto isExpr = std::make_unique<IsExpr>();
            isExpr->loc = opTok.loc;
            isExpr->value = std::move(lhs);
            auto [segs, startLoc] = parseQualifiedIdentSegments();
            isExpr->typeName = std::move(segs);
            lhs = std::move(isExpr);
            continue;
        }
        if (peek().kind == TokenKind::KeywordAs && min_prec <= 3)
        {
            auto opTok = peek();
            consume(); // AS
            auto asExpr = std::make_unique<AsExpr>();
            asExpr->loc = opTok.loc;
            asExpr->value = std::move(lhs);
            auto [segs, startLoc] = parseQualifiedIdentSegments();
            asExpr->typeName = std::move(segs);
            lhs = std::move(asExpr);
            continue;
        }

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
        auto e = makeFloatExpr(std::strtod(lex.c_str(), nullptr), loc);
        auto *floatExpr = static_cast<FloatExpr *>(e.get());
        if (suffix == '!')
            floatExpr->suffix = FloatExpr::Suffix::Single;
        else if (suffix == '#')
            floatExpr->suffix = FloatExpr::Suffix::Double;
        consume();
        return e;
    }

    int64_t v = std::strtoll(lex.c_str(), nullptr, 10);
    auto e = makeIntExpr(v, loc);
    auto *intExpr = static_cast<IntExpr *>(e.get());
    if (suffix == '%')
        intExpr->suffix = IntExpr::Suffix::Integer;
    else if (suffix == '&')
        intExpr->suffix = IntExpr::Suffix::Long;
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
    auto loc = peek().loc;
    std::string decoded;
    std::string err;
    if (!il::io::decodeEscapedString(peek().lexeme, decoded, &err))
    {
        emitError("B0003", loc, err);
        decoded = peek().lexeme;
    }
    consume();
    return makeStrExpr(decoded, loc);
}

/// @brief Parse a call to a BASIC builtin function.
/// @details Implements `builtin-call := BUILTIN '(' [expr {',' expr}] ')'` where the argument
/// structure is determined by the builtin's arity signature from the registry. Zero-argument
/// builtins are enforced at parse time; all others accept a flexible comma-separated list and
/// rely on semantic analysis for arity validation to provide clearer diagnostics.
/// @param builtin Enumerated builtin resolved by lookupBuiltin().
/// @param loc Source location of the builtin identifier.
/// @return Newly allocated builtin call expression with parsed arguments.
ExprPtr Parser::parseBuiltinCall(BuiltinCallExpr::Builtin builtin, il::support::SourceLoc loc)
{
    expect(TokenKind::LParen);
    std::vector<ExprPtr> args;

    const auto arity = getBuiltinArity(builtin);

    if (arity.maxArgs == 0)
    {
        // Zero-argument builtins: RND(), TIMER(), INKEY$(), GETKEY$()
        // Enforce empty argument list at parse time since this is unambiguous
        expect(TokenKind::RParen);
    }
    else
    {
        // All other builtins: parse flexible comma-separated arguments
        // The semantic analyzer will validate arity and provide specific diagnostics
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
ExprPtr Parser::parseVariableRef(std::string_view name, il::support::SourceLoc loc)
{
    auto v = std::make_unique<VarExpr>();
    v->loc = loc;
    v->name = name;
    return v;
}

/// @brief Parse an array element reference of the form `name(expr)` or `name(i,j,k)`.
/// @details After consuming the identifier, this helper expects an opening parenthesis, parses
/// comma-separated index expressions, and requires a closing parenthesis. The expect() calls emit
/// diagnostics on mismatched tokens but still advance so the parser can continue.
/// Supports multi-dimensional arrays: name(i,j,k).
/// @param name Array identifier.
/// @param loc Source location of the identifier.
/// @return Array reference expression with the parsed indices.
ExprPtr Parser::parseArrayRef(std::string_view name, il::support::SourceLoc loc)
{
    expect(TokenKind::LParen);

    // Parse comma-separated indices: arr(i,j,k)
    std::vector<ExprPtr> indexList;
    indexList.push_back(parseExpression());
    while (at(TokenKind::Comma))
    {
        consume(); // ','
        indexList.push_back(parseExpression());
    }

    expect(TokenKind::RParen);

    auto arr = std::make_unique<ArrayExpr>();
    arr->loc = loc;
    arr->name = name;

    // For backward compatibility with single-dimensional arrays:
    // - Populate only the deprecated 'index' field when exactly one index is present.
    // - Do NOT also populate 'indices' with a moved-from pointer, which is UB when accessed.
    // - For multi-dimensional arrays, populate 'indices' and leave 'index' null.
    if (indexList.size() == 1)
    {
        arr->index = std::move(indexList[0]);
        arr->indices.clear();
    }
    else
    {
        arr->indices = std::move(indexList);
    }

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

        if (arrays_.contains(name))
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

        // BUG-102 fix: Check if we're in a class and this call matches a method name.
        // If so, rewrite to a method call on ME.
        if (currentClass_)
        {
            // Check if this name matches a method in the current class
            for (const auto &member : currentClass_->members)
            {
                if (auto *method = dynamic_cast<MethodDecl *>(member.get()))
                {
                    // Case-insensitive comparison
                    auto equalsIgnoreCase = [](const std::string &lhs, const std::string &rhs)
                    {
                        if (lhs.size() != rhs.size())
                            return false;
                        for (std::size_t i = 0; i < lhs.size(); ++i)
                        {
                            unsigned char lc = static_cast<unsigned char>(lhs[i]);
                            unsigned char rc = static_cast<unsigned char>(rhs[i]);
                            if (std::toupper(lc) != std::toupper(rc))
                                return false;
                        }
                        return true;
                    };

                    if (equalsIgnoreCase(name, method->name))
                    {
                        // This is a method call - rewrite to ME.MethodName(args)
                        auto methodCall = std::make_unique<MethodCallExpr>();
                        methodCall->loc = loc;
                        methodCall->Expr::loc = loc;
                        methodCall->base = std::make_unique<MeExpr>();
                        methodCall->base->loc = loc;
                        methodCall->method = method->name; // Use actual method name
                        methodCall->args = std::move(args);
                        return methodCall;
                    }
                }
            }
        }

        // Not a method call - create regular CallExpr
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
        bool value = at(TokenKind::KeywordTrue);
        auto loc = peek().loc;
        consume();
        return makeBoolExpr(value, loc);
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

    // Support BASE-qualified member/method access by parsing BASE as a
    // primary that behaves like an identifier named "BASE". Lowering
    // detects VarExpr{"BASE"} to force direct base-class dispatch.
    if (at(TokenKind::KeywordBase))
    {
        auto v = std::make_unique<VarExpr>();
        v->loc = peek().loc;
        v->name = "BASE";
        consume();
        return v;
    }

    // BUG-CARDS-011 fix: Support NOTHING keyword as a null object reference.
    // Lowering detects VarExpr{"NOTHING"} and emits a null pointer.
    if (at(TokenKind::KeywordNothing))
    {
        auto v = std::make_unique<VarExpr>();
        v->loc = peek().loc;
        v->name = "NOTHING";
        consume();
        return v;
    }

    // ADDRESSOF keyword for obtaining function pointers (threading support).
    // Syntax: ADDRESSOF SubOrFunctionName
    if (at(TokenKind::KeywordAddressOf))
    {
        auto loc = peek().loc;
        consume(); // ADDRESSOF
        Token ident = expect(TokenKind::Identifier);
        auto expr = std::make_unique<AddressOfExpr>();
        expr->loc = loc;
        if (ident.kind == TokenKind::Identifier)
            expr->targetName = ident.lexeme;
        return expr;
    }

    if (at(TokenKind::KeywordLbound) || at(TokenKind::KeywordUbound))
        return parseBoundIntrinsic(peek().kind);

    if (at(TokenKind::KeywordLof) || at(TokenKind::KeywordEof) || at(TokenKind::KeywordLoc))
        return parseChannelIntrinsic(peek().kind);

    // BUG-OOP-041 fix: For soft keywords (FLOOR, COLOR, etc.), only treat as a
    // builtin call if followed by '('. Otherwise treat as a variable reference.
    // This allows using soft keywords as variable names: "IF floor <= 5 THEN"
    if (!at(TokenKind::Identifier))
    {
        // Only parse as builtin if this is NOT a soft keyword used as a variable,
        // or if it IS followed by '(' (i.e., actually being called as a function).
        bool isSoftKw = isSoftIdentToken(peek().kind) && peek().kind != TokenKind::Identifier;
        bool hasParenCall = (peek(1).kind == TokenKind::LParen);
        if (!isSoftKw || hasParenCall)
        {
            if (auto builtin = lookupBuiltin(peek().lexeme))
            {
                auto loc = peek().loc;
                consume();
                return parseBuiltinCall(*builtin, loc);
            }
        }
    }

    // BUG-OOP-021: Treat soft keywords (COLOR, FLOOR, etc.) as identifiers when
    // they appear in expression context. This allows using them as variable names.
    if (at(TokenKind::Identifier) ||
        (isSoftIdentToken(peek().kind) && peek().kind != TokenKind::Identifier))
    {
        // Attempt to parse a namespace-qualified call within an expression context.
        // This handles forms like A.B.F(...) and accepts single-dot A.F(...) only
        // when 'A' matches a namespace observed so far.
        Token head = peek();
        if (peek(1).kind == TokenKind::Dot)
        {
            // Non-destructive probe to see if we have Ident( . Ident )+ '('
            size_t i = 0;
            bool ok = true;
            bool sawAdditionalDot = false;
            // first ident and dot
            if (!(peek(i).kind == TokenKind::Identifier && peek(i + 1).kind == TokenKind::Dot))
                ok = false;
            if (ok)
            {
                i += 2;
                // BUG-OOP-040 fix: Use isMemberIdentToken() to allow keyword segments in
                // dotted namespaces (e.g., Viper.Random.Next, Viper.IO.File.Delete).
                while (isMemberIdentToken(peek(i).kind) && peek(i + 1).kind == TokenKind::Dot)
                {
                    sawAdditionalDot = true;
                    i += 2;
                }
                // Accept final segment as identifier or keyword (e.g.,
                // Viper.Text.StringBuilder.Append, Viper.Terminal.Color). (BUG-OOP-021)
                if (!(isMemberIdentToken(peek(i).kind) && peek(i + 1).kind == TokenKind::LParen))
                    ok = false;
                // BUG-082 fix: Only treat as qualified procedure call if the first identifier
                // is a known namespace. This applies to both single-dot (obj.Method) and
                // multi-dot (obj.field.Method) cases to prevent misclassifying method calls
                // on object members as namespace-qualified procedure calls.
                // BUG-004 fix: When head is not a known namespace, always reject as qualified
                // call and let parsePostfix handle it as member access + method call.
                // This fixes chained method calls like obj.field.Method().
                // Note: Use case-insensitive comparison since BASIC is case-insensitive.
                bool isKnownNamespace =
                    std::any_of(knownNamespaces_.begin(),
                                knownNamespaces_.end(),
                                [&head](const std::string &ns)
                                { return string_utils::iequals(head.lexeme, ns); });
                if (ok && !isKnownNamespace)
                {
                    ok = false;
                }
            }
            if (ok)
            {
                auto [segs, startLoc] = parseQualifiedIdentSegments();
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

                auto call = std::make_unique<CallExpr>();
                call->loc = startLoc;
                if (segs.size() > 1)
                    call->calleeQualified = segs;
                call->callee = JoinQualified(segs);
                call->args = std::move(args);
                return call;
            }
        }
        return parseArrayOrVar();
    }

    if (at(TokenKind::LParen))
    {
        consume();
        auto expr = parseExpression();
        expect(TokenKind::RParen);
        return expr;
    }

    return makeIntExpr(0, peek().loc);
}

/// @brief Parse a NEW expression allocating a class instance.
/// @return Newly allocated expression node.
ExprPtr Parser::parseNewExpression()
{
    auto loc = peek().loc;
    consume();

    std::string className;
    std::vector<std::string> qual;
    if (at(TokenKind::Identifier))
    {
        auto [segs, start] = parseQualifiedIdentSegments();
        (void)start;
        if (!segs.empty())
        {
            qual = std::move(segs);
            for (size_t i = 0; i < qual.size(); ++i)
            {
                if (i)
                    className.push_back('.');
                className += qual[i];
            }
        }
        else
        {
            Token classTok = expect(TokenKind::Identifier);
            if (classTok.kind == TokenKind::Identifier)
                className = classTok.lexeme;
        }
    }

    // BUG-CARDS-002 fix: Make parentheses optional for NEW expressions.
    // Allow both "NEW ClassName" and "NEW ClassName(args)" syntax.
    std::vector<ExprPtr> args;
    if (at(TokenKind::LParen))
    {
        consume(); // '('
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
    }

    auto expr = std::make_unique<NewExpr>();
    expr->loc = loc;
    expr->className = std::move(className);
    expr->qualifiedType = std::move(qual);
    expr->args = std::move(args);
    return expr;
}

std::pair<std::vector<std::string>, il::support::SourceLoc> Parser::parseQualifiedIdentSegments()
{
    std::vector<std::string> segs;
    il::support::SourceLoc startLoc{};
    if (!at(TokenKind::Identifier))
        return {segs, startLoc};
    Token first = peek();
    startLoc = first.loc;
    consume();
    segs.push_back(first.lexeme);
    while (at(TokenKind::Dot))
    {
        consume();
        // Allow identifier or keyword segments inside qualified names.
        // This supports forms like Viper.Terminal.Print or Viper.Math.Floor. (BUG-OOP-021)
        if (isMemberIdentToken(peek().kind))
        {
            Token ident = peek();
            consume();
            segs.push_back(ident.lexeme);
            continue;
        }
        break;
    }
    return {segs, startLoc};
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
        // BUG-OOP-040 fix: Permit keyword tokens as member names in dotted access
        // to support runtime namespaces like Viper.Random.Next().
        if (!isMemberIdentToken(peek().kind))
        {
            // Preserve original expectation for diagnostics when not matching.
            Token ident = expect(TokenKind::Identifier);
            (void)ident; // fall through; error already emitted if not identifier
        }
        Token ident = peek();
        consume();
        std::string member;
        if (isMemberIdentToken(ident.kind))
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
