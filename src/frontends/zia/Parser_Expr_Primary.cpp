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

        std::vector<CallArg> args = parseCallArgs();

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
    if (check(TokenKind::KwMatch)) {
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
        if (isMatchExpr) {
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
    if (checkIdentifierLike()) {
        // Struct-literal detection: only when allowStructLiterals_ is set.
        // Disambiguate: peek(1) == '{' and (peek(2) == '}' or (peek(2) == Ident and peek(3) ==
        // '='))
        if (allowStructLiterals_) {
            auto nextKind = peek(1).kind;
            bool isStructLiteral = false;
            if (nextKind == TokenKind::LBrace) {
                auto k2 = peek(2).kind;
                if (k2 == TokenKind::RBrace) {
                    isStructLiteral = true; // empty struct literal: TypeName {}
                } else if ((k2 == TokenKind::Identifier) && peek(3).kind == TokenKind::Equal) {
                    isStructLiteral = true; // TypeName { field = expr }
                }
            }

            if (isStructLiteral) {
                std::string typeName = peek().text;
                advance(); // consume TypeName
                advance(); // consume '{'

                std::vector<StructLiteralExpr::Field> fields;
                while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                    if (!fields.empty()) {
                        if (!match(TokenKind::Comma))
                            break;
                        if (check(TokenKind::RBrace))
                            break; // trailing comma
                    }
                    SourceLoc fieldLoc = peek().loc;
                    if (!check(TokenKind::Identifier)) {
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
    if (match(TokenKind::LParen)) {
        // Check for unit literal () or lambda () => ...
        if (check(TokenKind::RParen)) {
            advance(); // consume )
            // Check for lambda with no parameters: () => body
            if (match(TokenKind::FatArrow)) {
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

        if (check(TokenKind::Identifier)) {
            // Check for Java-style: (Type name) where Type starts with uppercase
            if (next.kind == TokenKind::Identifier && !peek().text.empty() &&
                std::isupper(static_cast<unsigned char>(peek().text[0]))) {
                looksLikeLambda = true;
            }
            // Check for Swift-style: (name: Type)
            else if (next.kind == TokenKind::Colon) {
                looksLikeLambda = true;
            }
            // Check for generic type: (List[T] name)
            else if (next.kind == TokenKind::LBracket && !peek().text.empty() &&
                     std::isupper(static_cast<unsigned char>(peek().text[0]))) {
                looksLikeLambda = true;
            }
        }

        if (looksLikeLambda) {
            // Try to parse as lambda parameters
            std::vector<LambdaParam> params;

            do {
                LambdaParam param;

                if (!checkIdentifierLike()) {
                    error("expected parameter in lambda");
                    return nullptr;
                }

                // Check which style: Java (Type name) or Swift (name: Type)
                Token firstTok = advance();
                std::string first = firstTok.text;
                SourceLoc firstLoc = firstTok.loc;

                if (check(TokenKind::Colon)) {
                    // Swift style: name: Type
                    advance(); // consume :
                    param.name = first;
                    param.type = parseType();
                    if (!param.type)
                        return nullptr;
                } else if (checkIdentifierLike()) {
                    // Java style: Type name (name can be contextual keyword like 'value')
                    param.name = peek().text;
                    advance();
                    // Reconstruct the type from first token
                    param.type = std::make_unique<NamedType>(firstLoc, first);
                } else if (check(TokenKind::LBracket)) {
                    // Generic type: List[T] name
                    // We need to parse the type arguments
                    advance(); // consume [
                    std::vector<TypePtr> typeArgs;
                    do {
                        TypePtr arg = parseType();
                        if (!arg)
                            return nullptr;
                        typeArgs.push_back(std::move(arg));
                    } while (match(TokenKind::Comma));

                    if (!expect(TokenKind::RBracket, "]"))
                        return nullptr;

                    // Now we should have the parameter name (can be contextual keyword)
                    if (!checkIdentifierLike()) {
                        error("expected parameter name after type");
                        return nullptr;
                    }
                    param.name = peek().text;
                    advance();
                    param.type =
                        std::make_unique<GenericType>(firstLoc, first, std::move(typeArgs));
                } else {
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

            return std::make_unique<TupleExpr>(loc, std::move(elements));
        }

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        // Check if this is a single-param lambda: (x) => expr
        if (match(TokenKind::FatArrow)) {
            // Convert the expression to a lambda parameter
            if (first->kind == ExprKind::Ident) {
                auto *ident = static_cast<IdentExpr *>(first.get());
                std::vector<LambdaParam> params;
                LambdaParam param;
                param.name = ident->name;
                param.type = nullptr;
                params.push_back(std::move(param));
                return parseLambdaBody(loc, std::move(params));
            } else {
                error("expected identifier for lambda parameter");
                return nullptr;
            }
        }

        return first;
    }

    // List literal
    if (check(TokenKind::LBracket)) {
        return parseListLiteral();
    }

    // Map or Set literal
    if (check(TokenKind::LBrace)) {
        return parseMapOrSetLiteral();
    }

    error("expected expression");
    return nullptr;
}

ExprPtr Parser::parseMatchExpression(SourceLoc loc) {
    ExprPtr scrutinee;
    if (match(TokenKind::LParen)) {
        scrutinee = parseExpression();
        if (!scrutinee)
            return nullptr;
        if (!expect(TokenKind::RParen, ")"))
            return nullptr;
    } else {
        scrutinee = parseExpression();
        if (!scrutinee)
            return nullptr;
    }

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
            Token lbraceTok = advance(); // consume '{'
            SourceLoc blockLoc = lbraceTok.loc;
            std::vector<StmtPtr> statements;

            while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                StmtPtr stmt = parseStatement();
                if (!stmt) {
                    resyncAfterError();
                    continue;
                }
                statements.push_back(std::move(stmt));
            }

            if (!expect(TokenKind::RBrace, "}"))
                return nullptr;

            arm.body = std::make_unique<BlockExpr>(blockLoc, std::move(statements), nullptr);
        } else {
            arm.body = parseExpression();
            if (!arm.body)
                return nullptr;
        }

        arms.push_back(std::move(arm));

        // Optional comma or closing brace
        if (!check(TokenKind::RBrace)) {
            match(TokenKind::Comma);
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
        do {
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

ExprPtr Parser::parseLambdaBody(SourceLoc loc, std::vector<LambdaParam> params) {
    ExprPtr body;
    // Check for block body: => { ... }
    if (check(TokenKind::LBrace)) {
        SourceLoc blockLoc = peek().loc;
        advance(); // consume '{'

        std::vector<StmtPtr> statements;
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            StmtPtr stmt = parseStatement();
            if (!stmt) {
                resyncAfterError();
                continue;
            }
            statements.push_back(std::move(stmt));
        }

        if (!expect(TokenKind::RBrace, "}"))
            return nullptr;

        body = std::make_unique<BlockExpr>(blockLoc, std::move(statements), nullptr);
    } else {
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
ExprPtr Parser::parseInterpolatedString() {
    SourceLoc loc = peek().loc;

    // First part: "text${
    std::string firstPart = peek().stringValue;
    advance(); // consume StringStart

    // Build up a chain of string concatenations
    // Start with the first string part (could be empty)
    ExprPtr result = std::make_unique<StringLiteralExpr>(loc, std::move(firstPart));

    // Parse the first interpolated expression
    ExprPtr expr = parseExpression();
    if (!expr) {
        error("expected expression in string interpolation");
        return nullptr;
    }

    // Convert the expression to string if needed (we'll handle this in lowering)
    // For now, just concatenate with Add operator (string concat)
    result = std::make_unique<BinaryExpr>(loc, BinaryOp::Add, std::move(result), std::move(expr));

    // Now we should see either StringMid or StringEnd
    while (check(TokenKind::StringMid)) {
        // Get the middle string part
        std::string midPart = peek().stringValue;
        advance(); // consume StringMid

        // Concatenate the middle string part
        if (!midPart.empty()) {
            result = std::make_unique<BinaryExpr>(
                loc,
                BinaryOp::Add,
                std::move(result),
                std::make_unique<StringLiteralExpr>(loc, std::move(midPart)));
        }

        // Parse the next interpolated expression
        expr = parseExpression();
        if (!expr) {
            error("expected expression in string interpolation");
            return nullptr;
        }

        // Concatenate the expression
        result =
            std::make_unique<BinaryExpr>(loc, BinaryOp::Add, std::move(result), std::move(expr));
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
ExprPtr Parser::parseMapOrSetLiteral() {
    SourceLoc loc = peek().loc;
    advance(); // consume '{'

    // Empty brace = empty map (by convention)
    if (check(TokenKind::RBrace)) {
        advance();
        return std::make_unique<MapLiteralExpr>(loc, std::vector<MapEntry>{});
    }

    // Check if first element has colon (map) or not (set)
    ExprPtr first = parseExpression();
    if (!first)
        return nullptr;

    if (match(TokenKind::Colon)) {
        // It's a map
        std::vector<MapEntry> entries;

        ExprPtr firstValue = parseExpression();
        if (!firstValue)
            return nullptr;

        entries.push_back({std::move(first), std::move(firstValue)});

        while (match(TokenKind::Comma)) {
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
    } else {
        // It's a set
        std::vector<ExprPtr> elements;
        elements.push_back(std::move(first));

        while (match(TokenKind::Comma)) {
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

std::vector<CallArg> Parser::parseCallArgs() {
    std::vector<CallArg> args;

    if (check(TokenKind::RParen)) {
        return args;
    }

    do {
        CallArg arg;

        // Check for named argument: name: value
        if (checkIdentifierLike() && check(TokenKind::Colon, 1)) {
            Token nameTok = advance();
            advance(); // consume :
            arg.name = nameTok.text;
            arg.value = parseExpression();
        } else {
            arg.value = parseExpression();
        }

        if (!arg.value)
            return {};
        args.push_back(std::move(arg));
    } while (match(TokenKind::Comma));

    return args;
}

} // namespace il::frontends::zia
