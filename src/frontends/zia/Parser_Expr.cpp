//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Parser_Expr.cpp
/// @brief Expression parsing implementation for Zia parser.
///
/// @details This file implements the Parser class which builds an AST from
/// a token stream. Key implementation details:
///
/// ## Parsing Strategy
///
/// Uses recursive descent with one-token lookahead. Each grammar rule has
/// a corresponding parseXxx() method that:
/// 1. Checks current token to decide which production to use
/// 2. Consumes expected tokens with match() or expect()
/// 3. Recursively calls other parsing methods
/// 4. Constructs and returns AST nodes
///
/// ## Expression Parsing
///
/// Binary expressions use precedence climbing:
/// - parseAssignment() → parseTernary() → parseLogicalOr() → ...
/// - Each level calls the next higher precedence level for operands
/// - Loops to handle left-associative operators at same level
///
/// ## Error Recovery
///
/// On syntax errors:
/// 1. Report error with location and message
/// 2. Call resyncAfterError() to skip to next statement boundary
/// 3. Continue parsing to find additional errors
///
/// ## String Interpolation
///
/// Interpolated strings are parsed by:
/// 1. Detecting StringStart token
/// 2. Parsing expression between interpolation markers
/// 3. Collecting StringMid/StringEnd tokens
/// 4. Building string concatenation expressions
///
/// @see Parser.hpp for the class interface
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Parser.hpp"

namespace il::frontends::zia
{

//===----------------------------------------------------------------------===//
// Expression Parsing
//===----------------------------------------------------------------------===//

/// @brief Parse a match pattern, using speculation to distinguish structured patterns from
/// expressions.
/// @details Tries structured patterns (wildcard, constructor, binding) first; falls back to
///          expression pattern if not followed by a guard or fat arrow.
/// @return The parsed match arm pattern.
MatchArm::Pattern Parser::parseMatchPattern()
{
    MatchArm::Pattern pattern;

    // Speculatively parse a non-expression pattern and ensure it is followed by
    // either a guard or the fat arrow, otherwise fall back to expression pattern.
    {
        Speculation speculative(*this);
        MatchArm::Pattern candidate;
        if (parsePatternCore(candidate) && (check(TokenKind::KwIf) || check(TokenKind::FatArrow)))
        {
            speculative.commit();
            return candidate;
        }
    }

    pattern.kind = MatchArm::Pattern::Kind::Expression;
    pattern.literal = parseExpression();
    if (!pattern.literal)
    {
        error("expected pattern in match arm");
    }
    return pattern;
}

/// @brief Parse a core (non-expression) match pattern: wildcard, constructor, binding, literal, or
/// tuple.
/// @param[out] out The parsed pattern result.
/// @return True if a valid non-expression pattern was parsed, false otherwise.
bool Parser::parsePatternCore(MatchArm::Pattern &out)
{
    if (++patternDepth_ > kMaxPatternDepth)
    {
        --patternDepth_;
        error("pattern nesting too deep (limit: 256)");
        return false;
    }
    struct DepthGuard
    {
        unsigned &d;
        ~DepthGuard() { --d; }
    } patternGuard_{patternDepth_};

    if (check(TokenKind::Identifier))
    {
        Token nameTok = advance();
        std::string name = nameTok.text;

        if (name == "_")
        {
            out.kind = MatchArm::Pattern::Kind::Wildcard;
            return true;
        }

        if (name == "None")
        {
            out.kind = MatchArm::Pattern::Kind::Constructor;
            out.binding = std::move(name);
            return true;
        }

        if (match(TokenKind::LParen))
        {
            out.kind = MatchArm::Pattern::Kind::Constructor;
            out.binding = std::move(name);

            if (!check(TokenKind::RParen))
            {
                do
                {
                    MatchArm::Pattern subpattern;
                    if (!parsePatternCore(subpattern))
                    {
                        error("expected pattern in constructor pattern");
                        return false;
                    }
                    out.subpatterns.push_back(std::move(subpattern));
                } while (match(TokenKind::Comma));
            }

            if (!expect(TokenKind::RParen, ")"))
                return false;

            return true;
        }

        out.kind = MatchArm::Pattern::Kind::Binding;
        out.binding = std::move(name);
        return true;
    }

    if (check(TokenKind::IntegerLiteral) || check(TokenKind::StringLiteral) ||
        check(TokenKind::KwTrue) || check(TokenKind::KwFalse) || check(TokenKind::KwNull))
    {
        out.kind = MatchArm::Pattern::Kind::Literal;
        out.literal = parsePrimary();
        return out.literal != nullptr;
    }

    Token lparenTok;
    if (match(TokenKind::LParen, &lparenTok))
    {
        std::vector<MatchArm::Pattern> elements;

        if (!check(TokenKind::RParen))
        {
            do
            {
                MatchArm::Pattern subpattern;
                if (!parsePatternCore(subpattern))
                {
                    error("expected pattern in tuple pattern");
                    return false;
                }
                elements.push_back(std::move(subpattern));
            } while (match(TokenKind::Comma));
        }

        if (!expect(TokenKind::RParen, ")"))
            return false;

        // Single-element parenthesized pattern is not a tuple pattern.
        if (elements.size() <= 1)
            return false;

        if (elements.size() != 2)
        {
            error("tuple patterns must have exactly two elements");
            return false;
        }

        out.kind = MatchArm::Pattern::Kind::Tuple;
        out.subpatterns = std::move(elements);
        return true;
    }

    return false;
}

ExprPtr Parser::parseExpression()
{
    return parseAssignment();
}

ExprPtr Parser::parseAssignment()
{
    ExprPtr expr = parseTernary();
    if (!expr)
        return nullptr;

    Token eqTok;
    if (match(TokenKind::Equal, &eqTok))
    {
        SourceLoc loc = eqTok.loc;
        ExprPtr value = parseAssignment(); // right-associative
        if (!value)
            return nullptr;
        return std::make_unique<BinaryExpr>(
            loc, BinaryOp::Assign, std::move(expr), std::move(value));
    }

    return expr;
}

ExprPtr Parser::parseTernary()
{
    ExprPtr expr = parseRange();
    if (!expr)
        return nullptr;

    Token qTok;
    if (match(TokenKind::Question, &qTok))
    {
        SourceLoc loc = qTok.loc;
        ExprPtr thenExpr = parseExpression();
        if (!thenExpr)
            return nullptr;

        if (!expect(TokenKind::Colon, ":"))
            return nullptr;

        ExprPtr elseExpr = parseTernary();
        if (!elseExpr)
            return nullptr;

        return std::make_unique<TernaryExpr>(
            loc, std::move(expr), std::move(thenExpr), std::move(elseExpr));
    }

    return expr;
}

ExprPtr Parser::parseRange()
{
    ExprPtr expr = parseCoalesce();
    if (!expr)
        return nullptr;

    while (check(TokenKind::DotDot) || check(TokenKind::DotDotEqual))
    {
        Token opTok = advance();
        bool inclusive = opTok.kind == TokenKind::DotDotEqual;
        SourceLoc loc = opTok.loc;

        ExprPtr right = parseCoalesce();
        if (!right)
            return nullptr;

        expr = std::make_unique<RangeExpr>(loc, std::move(expr), std::move(right), inclusive);
    }

    return expr;
}

/// @brief Parse a null-coalescing expression (a ?? b).
/// @return The parsed expression, potentially wrapping a CoalesceExpr.
ExprPtr Parser::parseCoalesce()
{
    ExprPtr expr = parseLogicalOr();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::QuestionQuestion, &opTok))
    {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseLogicalOr();
        if (!right)
            return nullptr;

        expr = std::make_unique<CoalesceExpr>(loc, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseLogicalOr()
{
    ExprPtr expr = parseLogicalAnd();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::PipePipe, &opTok) || match(TokenKind::KwOr, &opTok))
    {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseLogicalAnd();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::Or, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseLogicalAnd()
{
    ExprPtr expr = parseBitwiseOr();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::AmpAmp, &opTok) || match(TokenKind::KwAnd, &opTok))
    {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseBitwiseOr();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::And, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseBitwiseOr()
{
    ExprPtr expr = parseBitwiseXor();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::Pipe, &opTok))
    {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseBitwiseXor();
        if (!right)
            return nullptr;

        expr =
            std::make_unique<BinaryExpr>(loc, BinaryOp::BitOr, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseBitwiseXor()
{
    ExprPtr expr = parseBitwiseAnd();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::Caret, &opTok))
    {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseBitwiseAnd();
        if (!right)
            return nullptr;

        expr =
            std::make_unique<BinaryExpr>(loc, BinaryOp::BitXor, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseBitwiseAnd()
{
    ExprPtr expr = parseEquality();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::Ampersand, &opTok))
    {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseEquality();
        if (!right)
            return nullptr;

        expr =
            std::make_unique<BinaryExpr>(loc, BinaryOp::BitAnd, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseEquality()
{
    ExprPtr expr = parseComparison();
    if (!expr)
        return nullptr;

    while (check(TokenKind::EqualEqual) || check(TokenKind::NotEqual))
    {
        Token opTok = advance();
        BinaryOp op = opTok.kind == TokenKind::EqualEqual ? BinaryOp::Eq : BinaryOp::Ne;
        SourceLoc loc = opTok.loc;

        ExprPtr right = parseComparison();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseComparison()
{
    ExprPtr expr = parseAdditive();
    if (!expr)
        return nullptr;

    while (check(TokenKind::Less) || check(TokenKind::LessEqual) || check(TokenKind::Greater) ||
           check(TokenKind::GreaterEqual))
    {
        Token opTok = advance();
        BinaryOp op;
        switch (opTok.kind)
        {
            case TokenKind::Less:
                op = BinaryOp::Lt;
                break;
            case TokenKind::LessEqual:
                op = BinaryOp::Le;
                break;
            case TokenKind::Greater:
                op = BinaryOp::Gt;
                break;
            case TokenKind::GreaterEqual:
                op = BinaryOp::Ge;
                break;
            default:
                error("expected comparison operator");
                return nullptr;
        }
        SourceLoc loc = opTok.loc;

        ExprPtr right = parseAdditive();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseAdditive()
{
    ExprPtr expr = parseMultiplicative();
    if (!expr)
        return nullptr;

    while (check(TokenKind::Plus) || check(TokenKind::Minus))
    {
        Token opTok = advance();
        BinaryOp op = opTok.kind == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Sub;
        SourceLoc loc = opTok.loc;

        ExprPtr right = parseMultiplicative();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseMultiplicative()
{
    ExprPtr expr = parseUnary();
    if (!expr)
        return nullptr;

    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent))
    {
        Token opTok = advance();
        BinaryOp op;
        switch (opTok.kind)
        {
            case TokenKind::Star:
                op = BinaryOp::Mul;
                break;
            case TokenKind::Slash:
                op = BinaryOp::Div;
                break;
            case TokenKind::Percent:
                op = BinaryOp::Mod;
                break;
            default:
                error("expected multiplicative operator");
                return nullptr;
        }
        SourceLoc loc = opTok.loc;

        ExprPtr right = parseUnary();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseUnary()
{
    if (++exprDepth_ > kMaxExprDepth)
    {
        --exprDepth_;
        error("expression nesting too deep (limit: 256)");
        return nullptr;
    }

    ExprPtr result;

    if (check(TokenKind::Minus) || check(TokenKind::Bang) || check(TokenKind::Tilde) ||
        check(TokenKind::KwNot) || check(TokenKind::Ampersand))
    {
        Token opTok = advance();
        UnaryOp op;
        switch (opTok.kind)
        {
            case TokenKind::Minus:
                op = UnaryOp::Neg;
                break;
            case TokenKind::Bang:
            case TokenKind::KwNot:
                op = UnaryOp::Not;
                break;
            case TokenKind::Tilde:
                op = UnaryOp::BitNot;
                break;
            case TokenKind::Ampersand:
                op = UnaryOp::AddressOf;
                break;
            default:
                --exprDepth_;
                error("expected unary operator");
                return nullptr;
        }
        SourceLoc loc = opTok.loc;

        // Special case: handle -9223372036854775808 (INT64_MIN)
        // The literal 9223372036854775808 can't be represented as int64_t,
        // but when negated it becomes INT64_MIN which is valid.
        if (op == UnaryOp::Neg && check(TokenKind::IntegerLiteral) && peek().requiresNegation)
        {
            advance(); // consume the integer literal
            --exprDepth_;
            // Return INT64_MIN directly as an integer literal
            return std::make_unique<IntLiteralExpr>(loc, INT64_MIN);
        }

        ExprPtr operand = parseUnary();
        if (!operand)
        {
            --exprDepth_;
            return nullptr;
        }

        result = std::make_unique<UnaryExpr>(loc, op, std::move(operand));
    }
    else
    {
        result = parsePostfix();
    }

    --exprDepth_;
    return result;
}

ExprPtr Parser::parsePostfixAndBinaryFrom(ExprPtr startExpr)
{
    // Parse postfix operators on the starting expression
    ExprPtr expr = parsePostfixFrom(std::move(startExpr));
    if (!expr)
        return nullptr;
    // Continue with binary operators (but not assignment)
    return parseBinaryFrom(std::move(expr));
}

/// @brief Parse binary operators from a pre-parsed left-hand expression.
/// @details Used by parsePostfixAndBinaryFrom to handle match arm patterns that
///          begin with an already-parsed primary expression.
/// @param expr The left-hand expression to extend with binary operators.
/// @return The extended expression.
ExprPtr Parser::parseBinaryFrom(ExprPtr expr)
{
    // Parse multiplicative ops
    while (true)
    {
        Token opTok;
        if (match(TokenKind::Star, &opTok))
        {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseUnary();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Mul, std::move(expr), std::move(right));
        }
        else if (match(TokenKind::Slash, &opTok))
        {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseUnary();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Div, std::move(expr), std::move(right));
        }
        else if (match(TokenKind::Percent, &opTok))
        {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseUnary();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Mod, std::move(expr), std::move(right));
        }
        else
        {
            break;
        }
    }
    // Parse additive ops
    while (true)
    {
        Token opTok;
        if (match(TokenKind::Plus, &opTok))
        {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseMultiplicative();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Add, std::move(expr), std::move(right));
        }
        else if (match(TokenKind::Minus, &opTok))
        {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseMultiplicative();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Sub, std::move(expr), std::move(right));
        }
        else
        {
            break;
        }
    }
    // Parse comparison ops
    while (true)
    {
        BinaryOp op;
        Token opTok;
        if (match(TokenKind::Less, &opTok))
            op = BinaryOp::Lt;
        else if (match(TokenKind::LessEqual, &opTok))
            op = BinaryOp::Le;
        else if (match(TokenKind::Greater, &opTok))
            op = BinaryOp::Gt;
        else if (match(TokenKind::GreaterEqual, &opTok))
            op = BinaryOp::Ge;
        else
            break;

        SourceLoc loc = opTok.loc;
        ExprPtr right = parseAdditive();
        if (!right)
            return nullptr;
        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }
    // Parse equality ops
    while (true)
    {
        BinaryOp op;
        Token opTok;
        if (match(TokenKind::EqualEqual, &opTok))
            op = BinaryOp::Eq;
        else if (match(TokenKind::NotEqual, &opTok))
            op = BinaryOp::Ne;
        else
            break;

        SourceLoc loc = opTok.loc;
        ExprPtr right = parseComparison();
        if (!right)
            return nullptr;
        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }
    // Parse logical and
    Token opTok;
    while (match(TokenKind::AmpAmp, &opTok) || match(TokenKind::KwAnd, &opTok))
    {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseEquality();
        if (!right)
            return nullptr;
        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::And, std::move(expr), std::move(right));
    }
    // Parse logical or
    while (match(TokenKind::PipePipe, &opTok) || match(TokenKind::KwOr, &opTok))
    {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseLogicalAnd();
        if (!right)
            return nullptr;
        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::Or, std::move(expr), std::move(right));
    }
    return expr;
}

/// @brief Apply postfix operators to a base expression.
/// @details Handles call, subscript, member access, optional chaining, is/as casts,
///          and try expressions in a loop until no more postfix operators match.
/// @param expr The base expression to extend.
/// @return The expression with all postfix operations applied.
ExprPtr Parser::parsePostfixFrom(ExprPtr expr)
{
    while (true)
    {
        Token opTok;
        if (match(TokenKind::LParen, &opTok))
        {
            // Function call
            SourceLoc loc = opTok.loc;
            std::vector<CallArg> args = parseCallArgs();
            if (!expect(TokenKind::RParen, ")"))
                return nullptr;

            expr = std::make_unique<CallExpr>(loc, std::move(expr), std::move(args));
        }
        else if (match(TokenKind::LBracket, &opTok))
        {
            // Index
            SourceLoc loc = opTok.loc;
            ExprPtr index = parseExpression();
            if (!index)
                return nullptr;

            if (!expect(TokenKind::RBracket, "]"))
                return nullptr;

            expr = std::make_unique<IndexExpr>(loc, std::move(expr), std::move(index));
        }
        else if (match(TokenKind::Dot, &opTok))
        {
            // Field access or tuple index
            SourceLoc loc = opTok.loc;

            // Check for tuple index access: tuple.0, tuple.1, etc.
            if (check(TokenKind::IntegerLiteral))
            {
                int64_t index = peek().intValue;
                advance(); // consume integer literal
                expr = std::make_unique<TupleIndexExpr>(
                    loc, std::move(expr), static_cast<size_t>(index));
            }
            else if (checkIdentifierLike())
            {
                std::string field = peek().text;
                advance(); // consume identifier
                expr = std::make_unique<FieldExpr>(loc, std::move(expr), std::move(field));
            }
            else
            {
                error("expected field name after '.'");
                return nullptr;
            }
        }
        else if (match(TokenKind::QuestionDot, &opTok))
        {
            // Optional chain
            SourceLoc loc = opTok.loc;
            if (!checkIdentifierLike())
            {
                error("expected field name after '?.'");
                return nullptr;
            }
            std::string field = peek().text;
            advance(); // consume identifier

            expr = std::make_unique<OptionalChainExpr>(loc, std::move(expr), std::move(field));
        }
        else if (match(TokenKind::KwIs, &opTok))
        {
            // Type check
            SourceLoc loc = opTok.loc;
            TypePtr type = parseType();
            if (!type)
                return nullptr;

            expr = std::make_unique<IsExpr>(loc, std::move(expr), std::move(type));
        }
        else if (match(TokenKind::KwAs, &opTok))
        {
            // Type cast
            SourceLoc loc = opTok.loc;
            TypePtr type = parseType();
            if (!type)
                return nullptr;

            expr = std::make_unique<AsExpr>(loc, std::move(expr), std::move(type));
        }
        else if (check(TokenKind::Question))
        {
            // Try expression: expr? - propagate null/error
            // Note: This is different from optional type T? or ternary a ? b : c
            const Token &next = peek(1);
            bool nextStartsExpr = false;
            switch (next.kind)
            {
                case TokenKind::Identifier:
                case TokenKind::IntegerLiteral:
                case TokenKind::NumberLiteral:
                case TokenKind::StringLiteral:
                case TokenKind::StringStart:
                case TokenKind::KwTrue:
                case TokenKind::KwFalse:
                case TokenKind::KwNull:
                case TokenKind::KwSelf:
                case TokenKind::KwSuper:
                case TokenKind::KwNew:
                case TokenKind::KwMatch:
                case TokenKind::LParen:
                case TokenKind::LBracket:
                case TokenKind::LBrace:
                case TokenKind::Minus:
                case TokenKind::Bang:
                case TokenKind::Tilde:
                case TokenKind::KwValue:
                    nextStartsExpr = true;
                    break;
                default:
                    break;
            }

            if (nextStartsExpr)
                break;

            Token qTok = advance();
            SourceLoc loc = qTok.loc;
            expr = std::make_unique<TryExpr>(loc, std::move(expr));
        }
        else
        {
            break;
        }
    }

    return expr;
}

ExprPtr Parser::parsePostfix()
{
    ExprPtr expr = parsePrimary();
    if (!expr)
        return nullptr;
    return parsePostfixFrom(std::move(expr));
}

ExprPtr Parser::parseMatchExpression(SourceLoc loc)
{
    ExprPtr scrutinee;
    if (match(TokenKind::LParen))
    {
        scrutinee = parseExpression();
        if (!scrutinee)
            return nullptr;
        if (!expect(TokenKind::RParen, ")"))
            return nullptr;
    }
    else
    {
        scrutinee = parseExpression();
        if (!scrutinee)
            return nullptr;
    }

    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    // Parse match arms
    std::vector<MatchArm> arms;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        MatchArm arm;

        arm.pattern = parseMatchPattern();
        if (match(TokenKind::KwIf))
        {
            arm.pattern.guard = parseExpression();
            if (!arm.pattern.guard)
                return nullptr;
        }

        // Expect =>
        if (!expect(TokenKind::FatArrow, "=>"))
            return nullptr;

        // Parse arm body (expression or block expression)
        if (check(TokenKind::LBrace))
        {
            Token lbraceTok = advance(); // consume '{'
            SourceLoc blockLoc = lbraceTok.loc;
            std::vector<StmtPtr> statements;

            while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
            {
                StmtPtr stmt = parseStatement();
                if (!stmt)
                {
                    resyncAfterError();
                    continue;
                }
                statements.push_back(std::move(stmt));
            }

            if (!expect(TokenKind::RBrace, "}"))
                return nullptr;

            arm.body = std::make_unique<BlockExpr>(blockLoc, std::move(statements), nullptr);
        }
        else
        {
            arm.body = parseExpression();
            if (!arm.body)
                return nullptr;
        }

        arms.push_back(std::move(arm));

        // Optional comma or closing brace
        if (!check(TokenKind::RBrace))
        {
            match(TokenKind::Comma);
        }
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return std::make_unique<MatchExpr>(loc, std::move(scrutinee), std::move(arms));
}

ExprPtr Parser::parsePrimary()
{
    SourceLoc loc = peek().loc;

    // Integer literal
    if (check(TokenKind::IntegerLiteral))
    {
        // Check for the special case where the literal requires negation
        // (i.e., 9223372036854775808 which only becomes valid as -9223372036854775808)
        if (peek().requiresNegation)
        {
            error("integer literal 9223372036854775808 out of range (use "
                  "-9223372036854775808 for minimum signed integer)");
            advance();
            return std::make_unique<IntLiteralExpr>(loc, 0);
        }
        int64_t value = peek().intValue;
        advance();
        return std::make_unique<IntLiteralExpr>(loc, value);
    }

    // Number literal
    if (check(TokenKind::NumberLiteral))
    {
        double value = peek().floatValue;
        advance();
        return std::make_unique<NumberLiteralExpr>(loc, value);
    }

    // String literal
    if (check(TokenKind::StringLiteral))
    {
        std::string value = peek().stringValue;
        advance();
        return std::make_unique<StringLiteralExpr>(loc, std::move(value));
    }

    // Interpolated string: "text${expr}text${expr}text"
    if (check(TokenKind::StringStart))
    {
        return parseInterpolatedString();
    }

    // Boolean literals
    if (match(TokenKind::KwTrue))
    {
        return std::make_unique<BoolLiteralExpr>(loc, true);
    }
    if (match(TokenKind::KwFalse))
    {
        return std::make_unique<BoolLiteralExpr>(loc, false);
    }

    // Null literal
    if (match(TokenKind::KwNull))
    {
        return std::make_unique<NullLiteralExpr>(loc);
    }

    // Self
    if (match(TokenKind::KwSelf))
    {
        return std::make_unique<SelfExpr>(loc);
    }

    // Super
    if (match(TokenKind::KwSuper))
    {
        return std::make_unique<SuperExprNode>(loc);
    }

    // New expression
    if (match(TokenKind::KwNew))
    {
        TypePtr type = parseType();
        if (!type)
            return nullptr;

        if (!expect(TokenKind::LParen, "("))
            return nullptr;

        std::vector<CallArg> args = parseCallArgs();

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        return std::make_unique<NewExpr>(loc, std::move(type), std::move(args));
    }

    // If-expression: `if cond { thenExpr } else { elseExpr }`
    // Only valid in expression position (parsePrimary is never called from statement dispatch).
    if (check(TokenKind::KwIf))
    {
        advance(); // consume 'if'
        ExprPtr cond = parseExpression();
        if (!cond)
            return nullptr;

        if (!expect(TokenKind::LBrace, "{"))
            return nullptr;
        ExprPtr thenExpr = parseExpression();
        if (!thenExpr)
            return nullptr;
        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        if (!expect(TokenKind::KwElse, "else"))
            return nullptr;
        if (!expect(TokenKind::LBrace, "{"))
            return nullptr;
        ExprPtr elseExpr = parseExpression();
        if (!elseExpr)
            return nullptr;
        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        return std::make_unique<IfExpr>(
            loc, std::move(cond), std::move(thenExpr), std::move(elseExpr));
    }

    // Match expression or 'match' used as identifier
    if (check(TokenKind::KwMatch))
    {
        // Look ahead: 'match' is a keyword (match expression) only when followed by
        // something that starts a scrutinee expression (identifier, literal, parenthesis).
        // When followed by ';', ')', ',', '.', operators, etc., treat it as a variable name.
        auto nextKind = peek(1).kind;
        bool isMatchExpr =
            (nextKind == TokenKind::Identifier || nextKind == TokenKind::IntegerLiteral ||
             nextKind == TokenKind::NumberLiteral || nextKind == TokenKind::StringLiteral ||
             nextKind == TokenKind::LParen || nextKind == TokenKind::KwTrue ||
             nextKind == TokenKind::KwFalse || nextKind == TokenKind::KwNull ||
             nextKind == TokenKind::KwSelf);
        if (isMatchExpr)
        {
            advance(); // consume 'match'
            return parseMatchExpression(loc);
        }
        // Otherwise treat 'match' as an identifier
        std::string name = peek().text;
        advance();
        return std::make_unique<IdentExpr>(loc, std::move(name));
    }

    // Identifier or struct-literal: `TypeName { field = expr, ... }`
    // Struct literals are only attempted when explicitly enabled (initializer/return context)
    // to avoid ambiguity with for/if/while block bodies.
    if (checkIdentifierLike())
    {
        // Struct-literal detection: only when allowStructLiterals_ is set.
        // Disambiguate: peek(1) == '{' and (peek(2) == '}' or (peek(2) == Ident and peek(3) ==
        // '='))
        if (allowStructLiterals_)
        {
            auto nextKind = peek(1).kind;
            bool isStructLiteral = false;
            if (nextKind == TokenKind::LBrace)
            {
                auto k2 = peek(2).kind;
                if (k2 == TokenKind::RBrace)
                {
                    isStructLiteral = true; // empty struct literal: TypeName {}
                }
                else if ((k2 == TokenKind::Identifier) && peek(3).kind == TokenKind::Equal)
                {
                    isStructLiteral = true; // TypeName { field = expr }
                }
            }

            if (isStructLiteral)
            {
                std::string typeName = peek().text;
                advance(); // consume TypeName
                advance(); // consume '{'

                std::vector<StructLiteralExpr::Field> fields;
                while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
                {
                    if (!fields.empty())
                    {
                        if (!match(TokenKind::Comma))
                            break;
                        if (check(TokenKind::RBrace))
                            break; // trailing comma
                    }
                    SourceLoc fieldLoc = peek().loc;
                    if (!check(TokenKind::Identifier))
                    {
                        error("Expected field name in struct literal");
                        break;
                    }
                    std::string fieldName = peek().text;
                    advance();
                    if (!expect(TokenKind::Equal, "="))
                        return nullptr;
                    ExprPtr fieldVal = parseExpression();
                    if (!fieldVal)
                        return nullptr;
                    fields.push_back(
                        StructLiteralExpr::Field{fieldName, std::move(fieldVal), fieldLoc});
                }
                if (!expect(TokenKind::RBrace, "}"))
                    return nullptr;
                return std::make_unique<StructLiteralExpr>(
                    loc, std::move(typeName), std::move(fields));
            }
        }

        // Plain identifier
        std::string name = peek().text;
        advance();
        return std::make_unique<IdentExpr>(loc, std::move(name));
    }

    // Parenthesized expression, unit literal, tuple, or lambda
    if (match(TokenKind::LParen))
    {
        // Check for unit literal () or lambda () => ...
        if (check(TokenKind::RParen))
        {
            advance(); // consume )
            // Check for lambda with no parameters: () => body
            if (match(TokenKind::FatArrow))
            {
                return parseLambdaBody(loc, {});
            }
            return std::make_unique<UnitLiteralExpr>(loc);
        }

        // Try to detect lambda parameter patterns:
        // - (Type name, ...) => expr     -- Java-style typed params
        // - (name: Type, ...) => expr    -- Swift-style typed params
        // - (name, ...) => expr          -- untyped params
        //
        // Heuristics:
        // 1. If we see Identifier followed by Identifier (and current is uppercase), likely Type
        // name
        // 2. If we see Identifier followed by :, likely name: Type
        // 3. Otherwise, could be expression or untyped lambda param

        const Token &next = peek(1);
        bool looksLikeLambda = false;

        if (check(TokenKind::Identifier))
        {
            // Check for Java-style: (Type name) where Type starts with uppercase
            if (next.kind == TokenKind::Identifier && !peek().text.empty() &&
                std::isupper(static_cast<unsigned char>(peek().text[0])))
            {
                looksLikeLambda = true;
            }
            // Check for Swift-style: (name: Type)
            else if (next.kind == TokenKind::Colon)
            {
                looksLikeLambda = true;
            }
            // Check for generic type: (List[T] name)
            else if (next.kind == TokenKind::LBracket && !peek().text.empty() &&
                     std::isupper(static_cast<unsigned char>(peek().text[0])))
            {
                looksLikeLambda = true;
            }
        }

        if (looksLikeLambda)
        {
            // Try to parse as lambda parameters
            std::vector<LambdaParam> params;

            do
            {
                LambdaParam param;

                if (!checkIdentifierLike())
                {
                    error("expected parameter in lambda");
                    return nullptr;
                }

                // Check which style: Java (Type name) or Swift (name: Type)
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
                        return nullptr;
                }
                else if (checkIdentifierLike())
                {
                    // Java style: Type name (name can be contextual keyword like 'value')
                    param.name = peek().text;
                    advance();
                    // Reconstruct the type from first token
                    param.type = std::make_unique<NamedType>(firstLoc, first);
                }
                else if (check(TokenKind::LBracket))
                {
                    // Generic type: List[T] name
                    // We need to parse the type arguments
                    advance(); // consume [
                    std::vector<TypePtr> typeArgs;
                    do
                    {
                        TypePtr arg = parseType();
                        if (!arg)
                            return nullptr;
                        typeArgs.push_back(std::move(arg));
                    } while (match(TokenKind::Comma));

                    if (!expect(TokenKind::RBracket, "]"))
                        return nullptr;

                    // Now we should have the parameter name (can be contextual keyword)
                    if (!checkIdentifierLike())
                    {
                        error("expected parameter name after type");
                        return nullptr;
                    }
                    param.name = peek().text;
                    advance();
                    param.type =
                        std::make_unique<GenericType>(firstLoc, first, std::move(typeArgs));
                }
                else
                {
                    // Untyped parameter or not a lambda - but we're already committed
                    // Treat as untyped parameter
                    param.name = first;
                    param.type = nullptr;
                }

                params.push_back(std::move(param));

            } while (match(TokenKind::Comma));

            if (!expect(TokenKind::RParen, ")"))
                return nullptr;

            if (!expect(TokenKind::FatArrow, "=>"))
                return nullptr;

            return parseLambdaBody(loc, std::move(params));
        }

        // Parse first expression - could be parenthesized expression or tuple
        ExprPtr first = parseExpression();
        if (!first)
            return nullptr;

        // Check for comma - if present, this is a tuple
        if (check(TokenKind::Comma))
        {
            std::vector<ExprPtr> elements;
            elements.push_back(std::move(first));

            while (match(TokenKind::Comma))
            {
                if (check(TokenKind::RParen))
                    break; // Allow trailing comma

                ExprPtr elem = parseExpression();
                if (!elem)
                    return nullptr;
                elements.push_back(std::move(elem));
            }

            if (!expect(TokenKind::RParen, ")"))
                return nullptr;

            return std::make_unique<TupleExpr>(loc, std::move(elements));
        }

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        // Check if this is a single-param lambda: (x) => expr
        if (match(TokenKind::FatArrow))
        {
            // Convert the expression to a lambda parameter
            if (first->kind == ExprKind::Ident)
            {
                auto *ident = static_cast<IdentExpr *>(first.get());
                std::vector<LambdaParam> params;
                LambdaParam param;
                param.name = ident->name;
                param.type = nullptr;
                params.push_back(std::move(param));
                return parseLambdaBody(loc, std::move(params));
            }
            else
            {
                error("expected identifier for lambda parameter");
                return nullptr;
            }
        }

        return first;
    }

    // List literal
    if (check(TokenKind::LBracket))
    {
        return parseListLiteral();
    }

    // Map or Set literal
    if (check(TokenKind::LBrace))
    {
        return parseMapOrSetLiteral();
    }

    error("expected expression");
    return nullptr;
}

/// @brief Parse a list literal expression ([elem, elem, ...]).
/// @return The parsed ListLiteralExpr, or nullptr on error.
ExprPtr Parser::parseListLiteral()
{
    SourceLoc loc = peek().loc;
    advance(); // consume '['

    std::vector<ExprPtr> elements;

    if (!check(TokenKind::RBracket))
    {
        do
        {
            ExprPtr elem = parseExpression();
            if (!elem)
                return nullptr;
            elements.push_back(std::move(elem));
        } while (match(TokenKind::Comma));
    }

    if (!expect(TokenKind::RBracket, "]"))
        return nullptr;

    return std::make_unique<ListLiteralExpr>(loc, std::move(elements));
}

ExprPtr Parser::parseLambdaBody(SourceLoc loc, std::vector<LambdaParam> params)
{
    ExprPtr body;
    // Check for block body: => { ... }
    if (check(TokenKind::LBrace))
    {
        SourceLoc blockLoc = peek().loc;
        advance(); // consume '{'

        std::vector<StmtPtr> statements;
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
        {
            StmtPtr stmt = parseStatement();
            if (!stmt)
            {
                resyncAfterError();
                continue;
            }
            statements.push_back(std::move(stmt));
        }

        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        body = std::make_unique<BlockExpr>(blockLoc, std::move(statements), nullptr);
    }
    else
    {
        body = parseExpression();
    }
    if (!body)
        return nullptr;
    return std::make_unique<LambdaExpr>(loc, std::move(params), nullptr, std::move(body));
}

/// @brief Parse an interpolated string ("text${expr}text").
/// @details Consumes StringStart, alternates between expressions and StringMid tokens,
///          and builds a chain of BinaryExpr(Add) concatenations ending with StringEnd.
/// @return The concatenated string expression, or nullptr on error.
ExprPtr Parser::parseInterpolatedString()
{
    SourceLoc loc = peek().loc;

    // First part: "text${
    std::string firstPart = peek().stringValue;
    advance(); // consume StringStart

    // Build up a chain of string concatenations
    // Start with the first string part (could be empty)
    ExprPtr result = std::make_unique<StringLiteralExpr>(loc, std::move(firstPart));

    // Parse the first interpolated expression
    ExprPtr expr = parseExpression();
    if (!expr)
    {
        error("expected expression in string interpolation");
        return nullptr;
    }

    // Convert the expression to string if needed (we'll handle this in lowering)
    // For now, just concatenate with Add operator (string concat)
    result = std::make_unique<BinaryExpr>(loc, BinaryOp::Add, std::move(result), std::move(expr));

    // Now we should see either StringMid or StringEnd
    while (check(TokenKind::StringMid))
    {
        // Get the middle string part
        std::string midPart = peek().stringValue;
        advance(); // consume StringMid

        // Concatenate the middle string part
        if (!midPart.empty())
        {
            result = std::make_unique<BinaryExpr>(
                loc,
                BinaryOp::Add,
                std::move(result),
                std::make_unique<StringLiteralExpr>(loc, std::move(midPart)));
        }

        // Parse the next interpolated expression
        expr = parseExpression();
        if (!expr)
        {
            error("expected expression in string interpolation");
            return nullptr;
        }

        // Concatenate the expression
        result =
            std::make_unique<BinaryExpr>(loc, BinaryOp::Add, std::move(result), std::move(expr));
    }

    // Must end with StringEnd
    if (!check(TokenKind::StringEnd))
    {
        error("expected end of interpolated string");
        return nullptr;
    }

    // Get the final string part
    std::string endPart = peek().stringValue;
    advance(); // consume StringEnd

    // Concatenate the final string part (if not empty)
    if (!endPart.empty())
    {
        result = std::make_unique<BinaryExpr>(
            loc,
            BinaryOp::Add,
            std::move(result),
            std::make_unique<StringLiteralExpr>(loc, std::move(endPart)));
    }

    return result;
}

/// @brief Parse a map literal ({key: value, ...}) or set literal ({elem, ...}).
/// @details Disambiguates maps from sets by checking for a colon after the first element.
///          An empty brace pair {} is parsed as an empty map.
/// @return The parsed MapLiteralExpr or SetLiteralExpr, or nullptr on error.
ExprPtr Parser::parseMapOrSetLiteral()
{
    SourceLoc loc = peek().loc;
    advance(); // consume '{'

    // Empty brace = empty map (by convention)
    if (check(TokenKind::RBrace))
    {
        advance();
        return std::make_unique<MapLiteralExpr>(loc, std::vector<MapEntry>{});
    }

    // Check if first element has colon (map) or not (set)
    ExprPtr first = parseExpression();
    if (!first)
        return nullptr;

    if (match(TokenKind::Colon))
    {
        // It's a map
        std::vector<MapEntry> entries;

        ExprPtr firstValue = parseExpression();
        if (!firstValue)
            return nullptr;

        entries.push_back({std::move(first), std::move(firstValue)});

        while (match(TokenKind::Comma))
        {
            ExprPtr key = parseExpression();
            if (!key)
                return nullptr;

            if (!expect(TokenKind::Colon, ":"))
                return nullptr;

            ExprPtr value = parseExpression();
            if (!value)
                return nullptr;

            entries.push_back({std::move(key), std::move(value)});
        }

        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        return std::make_unique<MapLiteralExpr>(loc, std::move(entries));
    }
    else
    {
        // It's a set
        std::vector<ExprPtr> elements;
        elements.push_back(std::move(first));

        while (match(TokenKind::Comma))
        {
            ExprPtr elem = parseExpression();
            if (!elem)
                return nullptr;
            elements.push_back(std::move(elem));
        }

        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        return std::make_unique<SetLiteralExpr>(loc, std::move(elements));
    }
}

std::vector<CallArg> Parser::parseCallArgs()
{
    std::vector<CallArg> args;

    if (check(TokenKind::RParen))
    {
        return args;
    }

    do
    {
        CallArg arg;

        // Check for named argument: name: value
        if (checkIdentifierLike() && check(TokenKind::Colon, 1))
        {
            Token nameTok = advance();
            advance(); // consume :
            arg.name = nameTok.text;
            arg.value = parseExpression();
        }
        else
        {
            arg.value = parseExpression();
        }

        if (!arg.value)
            return {};
        args.push_back(std::move(arg));
    } while (match(TokenKind::Comma));

    return args;
}

} // namespace il::frontends::zia
