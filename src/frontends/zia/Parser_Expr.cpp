//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/zia/Parser_Expr.cpp
// Purpose: Precedence-climbing expression parsing core for the Zia parser —
//          assignment, ternary, binary operators, unary, and postfix handling.
// Key invariants:
//   - All methods are member functions of Parser declared in Parser.hpp
//   - Expression nesting depth bounded by kMaxExprDepth (256)
// Ownership/Lifetime:
//   - Parser borrows Lexer and DiagnosticEngine references
// Links: src/frontends/zia/Parser.hpp,
//        src/frontends/zia/Parser_Expr_Primary.cpp,
//        src/frontends/zia/Parser_Expr_Pattern.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Parser.hpp"

namespace il::frontends::zia {

//===----------------------------------------------------------------------===//
// Expression Parsing
//===----------------------------------------------------------------------===//

ExprPtr Parser::parseExpression() {
    return parseAssignment();
}

/// @brief Clone an lvalue expression for compound assignment desugaring.
/// @details Only handles lvalue forms: IdentExpr, FieldExpr, IndexExpr, SelfExpr.
static ExprPtr cloneLvalueExpr(Expr *expr) {
    if (!expr)
        return nullptr;

    switch (expr->kind) {
        case ExprKind::Ident: {
            auto *id = static_cast<IdentExpr *>(expr);
            return std::make_unique<IdentExpr>(id->loc, id->name);
        }
        case ExprKind::SelfExpr:
            return std::make_unique<SelfExpr>(expr->loc);
        case ExprKind::Field: {
            auto *field = static_cast<FieldExpr *>(expr);
            return std::make_unique<FieldExpr>(
                field->loc, cloneLvalueExpr(field->base.get()), field->field);
        }
        case ExprKind::Index: {
            auto *idx = static_cast<IndexExpr *>(expr);
            return std::make_unique<IndexExpr>(
                idx->loc, cloneLvalueExpr(idx->base.get()), cloneLvalueExpr(idx->index.get()));
        }
        default:
            // For other expression types (e.g., int literals used as index),
            // reconstruct as literal if possible
            if (expr->kind == ExprKind::IntLiteral) {
                auto *lit = static_cast<IntLiteralExpr *>(expr);
                return std::make_unique<IntLiteralExpr>(lit->loc, lit->value);
            }
            if (expr->kind == ExprKind::StringLiteral) {
                auto *lit = static_cast<StringLiteralExpr *>(expr);
                return std::make_unique<StringLiteralExpr>(lit->loc, lit->value);
            }
            return nullptr;
    }
}

ExprPtr Parser::parseAssignment() {
    ExprPtr expr = parseTernary();
    if (!expr)
        return nullptr;

    Token eqTok;
    if (match(TokenKind::Equal, &eqTok)) {
        SourceLoc loc = eqTok.loc;
        ExprPtr value = parseAssignment(); // right-associative
        if (!value)
            return nullptr;
        return std::make_unique<BinaryExpr>(
            loc, BinaryOp::Assign, std::move(expr), std::move(value));
    }

    // Compound assignment operators: +=, -=, *=, /=, %=
    // Desugar: a += b  ->  a = a + b
    auto compoundOp = [&](TokenKind tk) -> BinaryOp {
        switch (tk) {
            case TokenKind::PlusEqual:
                return BinaryOp::Add;
            case TokenKind::MinusEqual:
                return BinaryOp::Sub;
            case TokenKind::StarEqual:
                return BinaryOp::Mul;
            case TokenKind::SlashEqual:
                return BinaryOp::Div;
            case TokenKind::PercentEqual:
                return BinaryOp::Mod;
            default:
                return BinaryOp::Add; // unreachable
        }
    };

    Token compTok;
    if (match(TokenKind::PlusEqual, &compTok) || match(TokenKind::MinusEqual, &compTok) ||
        match(TokenKind::StarEqual, &compTok) || match(TokenKind::SlashEqual, &compTok) ||
        match(TokenKind::PercentEqual, &compTok)) {
        SourceLoc loc = compTok.loc;
        BinaryOp op = compoundOp(compTok.kind);

        // Clone the LHS for the read side of the compound operation
        ExprPtr lhsClone = cloneLvalueExpr(expr.get());
        if (!lhsClone) {
            error("compound assignment target must be an lvalue");
            return nullptr;
        }

        ExprPtr rhs = parseAssignment(); // right-associative
        if (!rhs)
            return nullptr;

        // Build: lhs = lhsClone op rhs
        auto arithExpr = std::make_unique<BinaryExpr>(loc, op, std::move(lhsClone), std::move(rhs));
        return std::make_unique<BinaryExpr>(
            loc, BinaryOp::Assign, std::move(expr), std::move(arithExpr));
    }

    return expr;
}

ExprPtr Parser::parseTernary() {
    ExprPtr expr = parseRange();
    if (!expr)
        return nullptr;

    Token qTok;
    if (match(TokenKind::Question, &qTok)) {
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

ExprPtr Parser::parseRange() {
    ExprPtr expr = parseCoalesce();
    if (!expr)
        return nullptr;

    while (check(TokenKind::DotDot) || check(TokenKind::DotDotEqual)) {
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
ExprPtr Parser::parseCoalesce() {
    ExprPtr expr = parseLogicalOr();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::QuestionQuestion, &opTok)) {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseLogicalOr();
        if (!right)
            return nullptr;

        expr = std::make_unique<CoalesceExpr>(loc, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseLogicalOr() {
    ExprPtr expr = parseLogicalAnd();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::PipePipe, &opTok) || match(TokenKind::KwOr, &opTok)) {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseLogicalAnd();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::Or, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseLogicalAnd() {
    ExprPtr expr = parseBitwiseOr();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::AmpAmp, &opTok) || match(TokenKind::KwAnd, &opTok)) {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseBitwiseOr();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::And, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseBitwiseOr() {
    ExprPtr expr = parseBitwiseXor();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::Pipe, &opTok)) {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseBitwiseXor();
        if (!right)
            return nullptr;

        expr =
            std::make_unique<BinaryExpr>(loc, BinaryOp::BitOr, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseBitwiseXor() {
    ExprPtr expr = parseBitwiseAnd();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::Caret, &opTok)) {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseBitwiseAnd();
        if (!right)
            return nullptr;

        expr =
            std::make_unique<BinaryExpr>(loc, BinaryOp::BitXor, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseBitwiseAnd() {
    ExprPtr expr = parseEquality();
    if (!expr)
        return nullptr;

    Token opTok;
    while (match(TokenKind::Ampersand, &opTok)) {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseEquality();
        if (!right)
            return nullptr;

        expr =
            std::make_unique<BinaryExpr>(loc, BinaryOp::BitAnd, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseEquality() {
    ExprPtr expr = parseComparison();
    if (!expr)
        return nullptr;

    while (check(TokenKind::EqualEqual) || check(TokenKind::NotEqual)) {
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

ExprPtr Parser::parseComparison() {
    ExprPtr expr = parseAdditive();
    if (!expr)
        return nullptr;

    while (check(TokenKind::Less) || check(TokenKind::LessEqual) || check(TokenKind::Greater) ||
           check(TokenKind::GreaterEqual)) {
        Token opTok = advance();
        BinaryOp op;
        switch (opTok.kind) {
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

ExprPtr Parser::parseAdditive() {
    ExprPtr expr = parseMultiplicative();
    if (!expr)
        return nullptr;

    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
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

ExprPtr Parser::parseMultiplicative() {
    ExprPtr expr = parseUnary();
    if (!expr)
        return nullptr;

    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
        Token opTok = advance();
        BinaryOp op;
        switch (opTok.kind) {
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

ExprPtr Parser::parseUnary() {
    if (++exprDepth_ > kMaxExprDepth) {
        --exprDepth_;
        error("expression nesting too deep (limit: 256)");
        return nullptr;
    }

    ExprPtr result;

    // await expr -- parsed as a unary prefix like ! or -
    if (check(TokenKind::KwAwait)) {
        Token awaitTok = advance();
        SourceLoc loc = awaitTok.loc;
        ExprPtr operand = parseUnary();
        if (!operand) {
            --exprDepth_;
            return nullptr;
        }
        --exprDepth_;
        return std::make_unique<AwaitExpr>(loc, std::move(operand));
    }

    if (check(TokenKind::Minus) || check(TokenKind::Bang) || check(TokenKind::Tilde) ||
        check(TokenKind::KwNot) || check(TokenKind::Ampersand)) {
        Token opTok = advance();
        UnaryOp op;
        switch (opTok.kind) {
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
        if (op == UnaryOp::Neg && check(TokenKind::IntegerLiteral) && peek().requiresNegation) {
            advance(); // consume the integer literal
            --exprDepth_;
            // Return INT64_MIN directly as an integer literal
            return std::make_unique<IntLiteralExpr>(loc, INT64_MIN);
        }

        ExprPtr operand = parseUnary();
        if (!operand) {
            --exprDepth_;
            return nullptr;
        }

        result = std::make_unique<UnaryExpr>(loc, op, std::move(operand));
    } else {
        result = parsePostfix();
    }

    --exprDepth_;
    return result;
}

ExprPtr Parser::parsePostfixAndBinaryFrom(ExprPtr startExpr) {
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
ExprPtr Parser::parseBinaryFrom(ExprPtr expr) {
    // Parse multiplicative ops
    while (true) {
        Token opTok;
        if (match(TokenKind::Star, &opTok)) {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseUnary();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Mul, std::move(expr), std::move(right));
        } else if (match(TokenKind::Slash, &opTok)) {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseUnary();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Div, std::move(expr), std::move(right));
        } else if (match(TokenKind::Percent, &opTok)) {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseUnary();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Mod, std::move(expr), std::move(right));
        } else {
            break;
        }
    }
    // Parse additive ops
    while (true) {
        Token opTok;
        if (match(TokenKind::Plus, &opTok)) {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseMultiplicative();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Add, std::move(expr), std::move(right));
        } else if (match(TokenKind::Minus, &opTok)) {
            SourceLoc loc = opTok.loc;
            ExprPtr right = parseMultiplicative();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Sub, std::move(expr), std::move(right));
        } else {
            break;
        }
    }
    // Parse comparison ops
    while (true) {
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
    while (true) {
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
    while (match(TokenKind::AmpAmp, &opTok) || match(TokenKind::KwAnd, &opTok)) {
        SourceLoc loc = opTok.loc;
        ExprPtr right = parseEquality();
        if (!right)
            return nullptr;
        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::And, std::move(expr), std::move(right));
    }
    // Parse logical or
    while (match(TokenKind::PipePipe, &opTok) || match(TokenKind::KwOr, &opTok)) {
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
ExprPtr Parser::parsePostfixFrom(ExprPtr expr) {
    while (true) {
        Token opTok;
        if (match(TokenKind::LParen, &opTok)) {
            // Function call
            SourceLoc loc = opTok.loc;
            std::vector<CallArg> args = parseCallArgs();
            if (!expect(TokenKind::RParen, ")"))
                return nullptr;

            expr = std::make_unique<CallExpr>(loc, std::move(expr), std::move(args));
        } else if (match(TokenKind::LBracket, &opTok)) {
            // Index
            SourceLoc loc = opTok.loc;
            ExprPtr index = parseExpression();
            if (!index)
                return nullptr;

            if (!expect(TokenKind::RBracket, "]"))
                return nullptr;

            expr = std::make_unique<IndexExpr>(loc, std::move(expr), std::move(index));
        } else if (match(TokenKind::Dot, &opTok)) {
            // Field access or tuple index
            SourceLoc loc = opTok.loc;

            // Check for tuple index access: tuple.0, tuple.1, etc.
            if (check(TokenKind::IntegerLiteral)) {
                int64_t index = peek().intValue;
                advance(); // consume integer literal
                expr = std::make_unique<TupleIndexExpr>(
                    loc, std::move(expr), static_cast<size_t>(index));
            } else if (checkIdentifierLike()) {
                std::string field = peek().text;
                advance(); // consume identifier
                expr = std::make_unique<FieldExpr>(loc, std::move(expr), std::move(field));
            } else {
                error("expected field name after '.'");
                return nullptr;
            }
        } else if (match(TokenKind::QuestionDot, &opTok)) {
            // Optional chain
            SourceLoc loc = opTok.loc;
            if (!checkIdentifierLike()) {
                error("expected field name after '?.'");
                return nullptr;
            }
            std::string field = peek().text;
            advance(); // consume identifier

            expr = std::make_unique<OptionalChainExpr>(loc, std::move(expr), std::move(field));
        } else if (match(TokenKind::KwIs, &opTok)) {
            // Type check
            SourceLoc loc = opTok.loc;
            TypePtr type = parseType();
            if (!type)
                return nullptr;

            expr = std::make_unique<IsExpr>(loc, std::move(expr), std::move(type));
        } else if (match(TokenKind::KwAs, &opTok)) {
            // Type cast
            SourceLoc loc = opTok.loc;
            TypePtr type = parseType();
            if (!type)
                return nullptr;

            expr = std::make_unique<AsExpr>(loc, std::move(expr), std::move(type));
        } else if (check(TokenKind::Question)) {
            // Try expression: expr? - propagate null/error
            // Note: This is different from optional type T? or ternary a ? b : c
            const Token &next = peek(1);
            bool nextStartsExpr = false;
            switch (next.kind) {
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
                case TokenKind::KwStruct:
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
        } else if (check(TokenKind::Bang)) {
            // Force-unwrap: expr! - asserts non-null, traps if null
            Token bangTok = advance();
            SourceLoc loc = bangTok.loc;
            expr = std::make_unique<ForceUnwrapExpr>(loc, std::move(expr));
        } else {
            break;
        }
    }

    return expr;
}

ExprPtr Parser::parsePostfix() {
    ExprPtr expr = parsePrimary();
    if (!expr)
        return nullptr;
    return parsePostfixFrom(std::move(expr));
}

} // namespace il::frontends::zia
