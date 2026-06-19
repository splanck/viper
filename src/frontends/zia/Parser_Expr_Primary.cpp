//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/zia/Parser_Expr_Primary.cpp
// Purpose: Primary expression parsing for the Zia parser — literals,
//          identifiers, parenthesized exprs, lambdas, list/map/set
//          literals, string interpolation, if/match expressions.
// Key invariants:
//   - All methods are member functions of Parser declared in Parser.hpp
//   - Precedence climbing flows: parsePrimary -> parsePostfix -> parseUnary -> ...
// Ownership/Lifetime:
//   - Parser borrows Lexer and DiagnosticEngine references
// Links: src/frontends/zia/Parser.hpp, src/frontends/zia/Parser_Expr.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Parser.hpp"

namespace il::frontends::zia {

ExprPtr Parser::parsePrimary() {
    SourceLoc loc = peek().loc;

    // Integer literal
    if (check(TokenKind::IntegerLiteral)) {
        // Check for the special case where the literal requires negation
        // (i.e., 9223372036854775808 which only becomes valid as -9223372036854775808)
        if (peek().requiresNegation) {
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
    if (check(TokenKind::NumberLiteral)) {
        double value = peek().floatValue;
        advance();
        return std::make_unique<NumberLiteralExpr>(loc, value);
    }

    // String literal
    if (check(TokenKind::StringLiteral)) {
        std::string value = peek().stringValue;
        advance();
        return std::make_unique<StringLiteralExpr>(loc, std::move(value));
    }

    // Interpolated string: "text${expr}text${expr}text"
    if (check(TokenKind::StringStart)) {
        return parseInterpolatedString();
    }

    // Boolean literals
    if (match(TokenKind::KwTrue)) {
        return std::make_unique<BoolLiteralExpr>(loc, true);
    }
    if (match(TokenKind::KwFalse)) {
        return std::make_unique<BoolLiteralExpr>(loc, false);
    }

    // Null literal
    if (match(TokenKind::KwNull)) {
        return std::make_unique<NullLiteralExpr>(loc);
    }

    // Self
    if (match(TokenKind::KwSelf)) {
        return std::make_unique<SelfExpr>(loc);
    }

    // Super
    if (match(TokenKind::KwSuper)) {
        return std::make_unique<SuperExprNode>(loc);
    }

    // New expression
    if (match(TokenKind::KwNew)) {
        TypePtr type = parseType();
        if (!type)
            return nullptr;

        if (!expect(TokenKind::LParen, "("))
            return nullptr;

        std::vector<CallArg> args;
        if (!parseCallArgs(args))
            return nullptr;

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        return std::make_unique<NewExpr>(loc, std::move(type), std::move(args));
    }

    // If-expression: `if cond { thenExpr } else { elseExpr }`
    // Only valid in expression position (parsePrimary is never called from statement dispatch).
    if (check(TokenKind::KwIf)) {
        advance(); // consume 'if'
        ExprPtr cond = parseExpression();
        if (!cond)
            return nullptr;

        if (!expect(TokenKind::LBrace, "{"))
            return nullptr;
        ExprPtr thenExpr = parseExpressionAllowingStructLiterals();
        if (!thenExpr)
            return nullptr;
        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        if (!expect(TokenKind::KwElse, "else"))
            return nullptr;
        if (!expect(TokenKind::LBrace, "{"))
            return nullptr;
        ExprPtr elseExpr = parseExpressionAllowingStructLiterals();
        if (!elseExpr)
            return nullptr;
        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        return std::make_unique<IfExpr>(
            loc, std::move(cond), std::move(thenExpr), std::move(elseExpr));
    }

    // Match expression or 'match' used as identifier
    if (check(TokenKind::KwMatch)) {
        if (isMatchExpressionAhead()) {
            advance(); // consume 'match'
            return parseMatchExpression(loc);
        }
        // Otherwise treat 'match' as an identifier
        std::string name = peek().text;
        advance();
        return std::make_unique<IdentExpr>(loc, std::move(name));
    }

    // Explicit collection literals: `set { ... }` / `map { ... }`
    // This provides an unambiguous empty-set form without reserving new keywords.
    if (check(TokenKind::Identifier) && peek().text == "set" && check(TokenKind::LBrace, 1)) {
        SourceLoc setLoc = peek().loc;
        advance(); // consume 'set'
        advance(); // consume '{'

        std::vector<ExprPtr> elements;
        if (!check(TokenKind::RBrace)) {
            while (true) {
                ExprPtr elem = parseExpressionAllowingStructLiterals();
                if (!elem)
                    return nullptr;
                if (check(TokenKind::Colon)) {
                    error("explicit set literal cannot contain key/value pairs");
                    return nullptr;
                }
                elements.push_back(std::move(elem));
                if (!match(TokenKind::Comma))
                    break;
                if (check(TokenKind::RBrace))
                    break;
            }
        }

        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;
        return std::make_unique<SetLiteralExpr>(setLoc, std::move(elements));
    }

    if (check(TokenKind::Identifier) && peek().text == "map" && check(TokenKind::LBrace, 1)) {
        SourceLoc mapLoc = peek().loc;
        advance(); // consume 'map'
        advance(); // consume '{'

        std::vector<MapEntry> entries;
        if (!check(TokenKind::RBrace)) {
            while (true) {
                ExprPtr key = parseExpressionAllowingStructLiterals();
                if (!key)
                    return nullptr;
                if (!expect(TokenKind::Colon, ":"))
                    return nullptr;
                ExprPtr value = parseExpressionAllowingStructLiterals();
                if (!value)
                    return nullptr;
                entries.push_back({std::move(key), std::move(value)});
                if (!match(TokenKind::Comma))
                    break;
                if (check(TokenKind::RBrace))
                    break;
            }
        }

        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;
        return std::make_unique<MapLiteralExpr>(mapLoc, std::move(entries));
    }

    // Identifier or struct-literal: `TypeName { field = expr, ... }` or `TypeName { field: expr,
    // ... }`
    if (checkIdentifierLike())
        return parseIdentifierOrStructLiteral(loc);

    // Parenthesized expression, unit literal, tuple, or lambda
    if (match(TokenKind::LParen))
        return parseParenthesizedExpr(loc);

    // List literal
    if (check(TokenKind::LBracket)) {
        return parseListLiteral();
    }

    // Map or Set literal
    if (check(TokenKind::LBrace)) {
        if (looksLikeBlockExpression())
            return parseBlockExpression();
        return parseMapOrSetLiteral();
    }

    error("expected expression");
    return nullptr;
}

ExprPtr Parser::parseIdentifierOrStructLiteral(SourceLoc loc) {
    {
        // Struct literals are only attempted when explicitly enabled
        // (initializer/return context) to avoid ambiguity with block bodies.
        // Disambiguate: TypeName or Module.TypeName followed by a field-list body.
        if (allowStructLiterals_) {
            size_t bodyOffset = 1;
            std::string candidateName = peek().text;
            while (peek(bodyOffset).kind == TokenKind::Dot &&
                   peek(bodyOffset + 1).kind == TokenKind::Identifier) {
                candidateName += ".";
                candidateName += peek(bodyOffset + 1).text;
                bodyOffset += 2;
            }

            auto nextKind = peek(bodyOffset).kind;
            bool isStructLiteral = false;
            if (nextKind == TokenKind::LBrace) {
                auto k2 = peek(bodyOffset + 1).kind;
                if (k2 == TokenKind::RBrace) {
                    isStructLiteral = true; // empty struct literal: TypeName {}
                } else if ((k2 == TokenKind::Identifier) &&
                           (peek(bodyOffset + 2).kind == TokenKind::Equal ||
                            peek(bodyOffset + 2).kind == TokenKind::Colon)) {
                    isStructLiteral = true; // TypeName { field = expr } / TypeName { field: expr }
                }
            }

            if (isStructLiteral) {
                std::string typeName = std::move(candidateName);
                for (size_t i = 0; i < bodyOffset; ++i)
                    advance(); // consume qualified type name
                advance();     // consume '{'

                std::vector<StructLiteralExpr::Field> fields;
                while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                    if (!fields.empty()) {
                        if (!match(TokenKind::Comma))
                            break;
                        if (check(TokenKind::RBrace))
                            break; // trailing comma
                    }
                    SourceLoc fieldLoc = peek().loc;
                    if (!checkIdentifierLike()) {
                        error("Expected field name in struct literal");
                        break;
                    }
                    std::string fieldName = peek().text;
                    advance();
                    if (!match(TokenKind::Equal) && !match(TokenKind::Colon)) {
                        error("Expected '=' or ':' after field name in struct literal");
                        return nullptr;
                    }
                    ExprPtr fieldVal = parseExpressionAllowingStructLiterals();
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
}

ExprPtr Parser::parseParenthesizedExpr(SourceLoc loc) {
    {
        // Check for unit literal () or lambda () => ...
        if (check(TokenKind::RParen)) {
            advance(); // consume )
            // Check for lambda with no parameters: () => body
            if (match(TokenKind::FatArrow)) {
                return parseLambdaBody(loc, {});
            }
            return std::make_unique<UnitLiteralExpr>(loc);
        }

        bool matchedUntypedLambda = false;
        std::vector<LambdaParam> matchedTypedLambdaParams;
        bool matchedTypedLambda = false;
        {
            Speculation speculative(*this);
            std::vector<LambdaParam> params;
            bool validLambda = true;
            bool missingTypeAnnotation = false;

            do {
                if (!checkIdentifierLike()) {
                    validLambda = false;
                    break;
                }

                Token nameTok = advance();
                LambdaParam param;
                param.name = nameTok.text;

                if (match(TokenKind::Colon)) {
                    param.type = parseType();
                    if (!param.type) {
                        validLambda = false;
                        break;
                    }
                } else {
                    missingTypeAnnotation = true;
                }

                params.push_back(std::move(param));
            } while (match(TokenKind::Comma));

            if (validLambda && match(TokenKind::RParen) && match(TokenKind::FatArrow)) {
                speculative.commit();
                if (missingTypeAnnotation) {
                    matchedUntypedLambda = true;
                } else {
                    matchedTypedLambdaParams = std::move(params);
                    matchedTypedLambda = true;
                }
            }
        }

        if (matchedUntypedLambda) {
            error("lambda parameters require explicit type annotations");
            // Recover by consuming the lambda body so the trailing tokens aren't
            // re-parsed as stray expressions (prevents cascading diagnostics).
            parseLambdaBody(loc, {});
            return nullptr;
        }

        if (matchedTypedLambda) {
            return parseLambdaBody(loc, std::move(matchedTypedLambdaParams));
        }

        // Parse first expression - could be parenthesized expression or tuple
        ExprPtr first = parseExpression();
        if (!first)
            return nullptr;

        // Check for comma - if present, this is a tuple
        if (check(TokenKind::Comma)) {
            std::vector<ExprPtr> elements;
            elements.push_back(std::move(first));

            while (match(TokenKind::Comma)) {
                if (check(TokenKind::RParen))
                    break; // Allow trailing comma

                ExprPtr elem = parseExpression();
                if (!elem)
                    return nullptr;
                elements.push_back(std::move(elem));
            }

            if (!expect(TokenKind::RParen, ")"))
                return nullptr;

            if (check(TokenKind::FatArrow)) {
                advance();
                error("lambda parameters require explicit type annotations");
                return nullptr;
            }

            return std::make_unique<TupleExpr>(loc, std::move(elements));
        }

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        if (check(TokenKind::FatArrow)) {
            advance();
            error("lambda parameters require explicit type annotations");
            return nullptr;
        }

        return first;
    }
}

ExprPtr Parser::parseMatchExpression(SourceLoc loc) {
    ExprPtr scrutinee = parseExpression();
    if (!scrutinee)
        return nullptr;

    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    // Parse match arms
    std::vector<MatchArm> arms;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        MatchArm arm;

        arm.pattern = parseMatchPattern();
        if (match(TokenKind::KwIf)) {
            arm.pattern.guard = parseExpression();
            if (!arm.pattern.guard)
                return nullptr;
        }

        // Expect =>
        if (!expect(TokenKind::FatArrow, "=>"))
            return nullptr;

        // Parse arm body (expression or block expression)
        if (check(TokenKind::LBrace)) {
            arm.body = parseBlockExpression();
            if (!arm.body)
                return nullptr;
        } else {
            arm.body = parseExpressionAllowingStructLiterals();
            if (!arm.body)
                return nullptr;
        }

        arms.push_back(std::move(arm));

        while (match(TokenKind::Semicolon) || match(TokenKind::Comma)) {
        }
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return std::make_unique<MatchExpr>(loc, std::move(scrutinee), std::move(arms));
}

/// @brief Parse a list literal expression ([elem, elem, ...]).
/// @return The parsed ListLiteralExpr, or nullptr on error.
ExprPtr Parser::parseListLiteral() {
    SourceLoc loc = peek().loc;
    advance(); // consume '['

    std::vector<ExprPtr> elements;

    if (!check(TokenKind::RBracket)) {
        while (true) {
            ExprPtr elem = parseExpressionAllowingStructLiterals();
            if (!elem)
                return nullptr;
            elements.push_back(std::move(elem));
            if (!match(TokenKind::Comma))
                break;
            if (check(TokenKind::RBracket))
                break;
        }
    }

    if (!expect(TokenKind::RBracket, "]"))
        return nullptr;

    return std::make_unique<ListLiteralExpr>(loc, std::move(elements));
}

ExprPtr Parser::parseLambdaBody(SourceLoc loc, std::vector<LambdaParam> params) {
    ExprPtr body;
    // Check for block body: => { ... }
    if (check(TokenKind::LBrace)) {
        body = parseBlockExpression();
    } else {
        body = parseExpressionAllowingStructLiterals();
    }
    if (!body)
        return nullptr;
    return std::make_unique<LambdaExpr>(loc, std::move(params), nullptr, std::move(body));
}

ExprPtr Parser::parseBlockExpression() {
    SourceLoc loc = peek().loc;
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    std::vector<StmtPtr> statements;
    ExprPtr value;

    auto startsBlockStatement = [](TokenKind kind) {
        switch (kind) {
            case TokenKind::KwVar:
            case TokenKind::KwFinal:
            case TokenKind::KwLet:
            case TokenKind::KwReturn:
            case TokenKind::KwBreak:
            case TokenKind::KwContinue:
            case TokenKind::KwDefer:
            case TokenKind::KwThrow:
            case TokenKind::KwGuard:
            case TokenKind::KwWhile:
            case TokenKind::KwFor:
            case TokenKind::KwTry:
                return true;
            default:
                return false;
        }
    };

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        if (isExpressionStart(peek().kind) && !startsBlockStatement(peek().kind)) {
            Speculation speculation(*this);
            ExprPtr candidate = parseExpressionAllowingStructLiterals();
            if (candidate && check(TokenKind::RBrace)) {
                speculation.commit();
                value = std::move(candidate);
                break;
            }
        }

        StmtPtr stmt = parseStatement();
        if (!stmt) {
            resyncAfterError();
            continue;
        }
        statements.push_back(std::move(stmt));
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return std::make_unique<BlockExpr>(loc, std::move(statements), std::move(value));
}

/// @brief Parse an interpolated string ("text${expr}text").
/// @details Consumes StringStart, alternates between expressions and StringMid tokens,
///          and builds a chain of BinaryExpr(Add) concatenations ending with StringEnd.
/// @return The concatenated string expression, or nullptr on error.
ExprPtr Parser::parseInterpolatedString() {
    SourceLoc loc = peek().loc;

    // First part: "text${
    std::string firstPart = peek().stringValue;
    advance(); // consume StringStart

    std::vector<ExprPtr> parts;
    parts.push_back(std::make_unique<StringLiteralExpr>(loc, std::move(firstPart)));

    // Parse the first interpolated expression
    ExprPtr expr = parseExpressionAllowingStructLiterals();
    if (!expr) {
        error("expected expression in string interpolation");
        return nullptr;
    }

    parts.push_back(std::move(expr));

    // Now we should see either StringMid or StringEnd
    while (check(TokenKind::StringMid)) {
        // Get the middle string part
        std::string midPart = peek().stringValue;
        advance(); // consume StringMid

        // Keep even empty middle segments: `${a}${b}` needs the empty
        // string between expressions so balanced concatenation cannot fold
        // adjacent numeric expressions as arithmetic.
        parts.push_back(std::make_unique<StringLiteralExpr>(loc, std::move(midPart)));

        // Parse the next interpolated expression
        expr = parseExpressionAllowingStructLiterals();
        if (!expr) {
            error("expected expression in string interpolation");
            return nullptr;
        }

        parts.push_back(std::move(expr));
    }

    // Must end with StringEnd
    if (!check(TokenKind::StringEnd)) {
        error("expected end of interpolated string");
        return nullptr;
    }

    // Get the final string part
    std::string endPart = peek().stringValue;
    advance(); // consume StringEnd

    // Concatenate the final string part (if not empty)
    if (!endPart.empty()) {
        parts.push_back(std::make_unique<StringLiteralExpr>(loc, std::move(endPart)));
    }

    while (parts.size() > 1) {
        std::vector<ExprPtr> next;
        next.reserve((parts.size() + 1) / 2);
        for (size_t i = 0; i < parts.size(); i += 2) {
            if (i + 1 == parts.size()) {
                next.push_back(std::move(parts[i]));
                continue;
            }
            next.push_back(std::make_unique<BinaryExpr>(
                loc, BinaryOp::Add, std::move(parts[i]), std::move(parts[i + 1])));
        }
        parts = std::move(next);
    }
    return std::move(parts.front());
}

/// @brief Parse a map literal ({key: value, ...}) or set literal ({elem, ...}).
/// @details Disambiguates maps from sets by checking for a colon after the first element.
///          An empty brace pair {} is parsed as an empty map.
/// @return The parsed MapLiteralExpr or SetLiteralExpr, or nullptr on error.
ExprPtr Parser::parseMapOrSetLiteral() {
    SourceLoc loc = peek().loc;
    advance(); // consume '{'

    // Empty brace = empty map (by convention)
    if (check(TokenKind::RBrace)) {
        advance();
        return std::make_unique<MapLiteralExpr>(loc, std::vector<MapEntry>{});
    }

    // Check if first element has colon (map) or not (set)
    ExprPtr first = parseExpressionAllowingStructLiterals();
    if (!first)
        return nullptr;

    if (match(TokenKind::Colon)) {
        // It's a map
        std::vector<MapEntry> entries;

        ExprPtr firstValue = parseExpressionAllowingStructLiterals();
        if (!firstValue)
            return nullptr;

        entries.push_back({std::move(first), std::move(firstValue)});

        while (match(TokenKind::Comma)) {
            if (check(TokenKind::RBrace))
                break;

            ExprPtr key = parseExpressionAllowingStructLiterals();
            if (!key)
                return nullptr;

            if (!expect(TokenKind::Colon, ":"))
                return nullptr;

            ExprPtr value = parseExpressionAllowingStructLiterals();
            if (!value)
                return nullptr;

            entries.push_back({std::move(key), std::move(value)});
        }

        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        return std::make_unique<MapLiteralExpr>(loc, std::move(entries));
    } else {
        // It's a set
        std::vector<ExprPtr> elements;
        elements.push_back(std::move(first));

        while (match(TokenKind::Comma)) {
            if (check(TokenKind::RBrace))
                break;

            ExprPtr elem = parseExpressionAllowingStructLiterals();
            if (!elem)
                return nullptr;
            elements.push_back(std::move(elem));
        }

        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        return std::make_unique<SetLiteralExpr>(loc, std::move(elements));
    }
}

bool Parser::parseCallArgs(std::vector<CallArg> &args) {
    if (check(TokenKind::RParen)) {
        return true;
    }

    while (true) {
        CallArg arg;

        // Check for named argument: name: value
        if (checkIdentifierLike() && check(TokenKind::Colon, 1)) {
            Token nameTok = advance();
            advance(); // consume :
            arg.name = nameTok.text;
            arg.value = parseExpressionAllowingStructLiterals();
        } else {
            arg.value = parseExpressionAllowingStructLiterals();
        }

        if (!arg.value)
            return false;
        args.push_back(std::move(arg));
        if (!match(TokenKind::Comma))
            break;
        if (check(TokenKind::RParen))
            break;
    }

    return true;
}

std::vector<CallArg> Parser::parseCallArgs() {
    std::vector<CallArg> args;
    if (!parseCallArgs(args))
        return {};
    return args;
}

} // namespace il::frontends::zia
