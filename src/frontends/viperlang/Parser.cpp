//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Parser.cpp
/// @brief Implementation of ViperLang recursive descent parser.
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

#include "frontends/viperlang/Parser.hpp"

namespace il::frontends::viperlang
{

Parser::Parser(Lexer &lexer, il::support::DiagnosticEngine &diag) : lexer_(lexer), diag_(diag)
{
    current_ = lexer_.next();
}

//===----------------------------------------------------------------------===//
// Token Handling
//===----------------------------------------------------------------------===//

const Token &Parser::peek() const
{
    return current_;
}

Token Parser::advance()
{
    Token prev = std::move(current_);
    current_ = lexer_.next();
    return prev;
}

bool Parser::check(TokenKind kind) const
{
    return current_.kind == kind;
}

bool Parser::match(TokenKind kind)
{
    if (check(kind))
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenKind kind, const char *what)
{
    if (check(kind))
    {
        advance();
        return true;
    }
    error(std::string("expected ") + what + ", got " + tokenKindToString(current_.kind));
    return false;
}

void Parser::resyncAfterError()
{
    while (!check(TokenKind::Eof))
    {
        if (check(TokenKind::Semicolon))
        {
            advance();
            return;
        }
        if (check(TokenKind::RBrace) || check(TokenKind::KwFunc) || check(TokenKind::KwValue) ||
            check(TokenKind::KwEntity) || check(TokenKind::KwInterface))
        {
            return;
        }
        advance();
    }
}

//===----------------------------------------------------------------------===//
// Error Handling
//===----------------------------------------------------------------------===//

void Parser::error(const std::string &message)
{
    errorAt(current_.loc, message);
}

void Parser::errorAt(SourceLoc loc, const std::string &message)
{
    hasError_ = true;
    diag_.report(il::support::Diagnostic{
        il::support::Severity::Error,
        message,
        loc,
        "V2000" // ViperLang parser error code
    });
}

//===----------------------------------------------------------------------===//
// Expression Parsing
//===----------------------------------------------------------------------===//

ExprPtr Parser::parseExpression()
{
    return parseAssignment();
}

ExprPtr Parser::parseAssignment()
{
    ExprPtr expr = parseTernary();
    if (!expr)
        return nullptr;

    if (match(TokenKind::Equal))
    {
        SourceLoc loc = current_.loc;
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

    if (match(TokenKind::Question))
    {
        SourceLoc loc = current_.loc;
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
        bool inclusive = check(TokenKind::DotDotEqual);
        SourceLoc loc = current_.loc;
        advance();

        ExprPtr right = parseCoalesce();
        if (!right)
            return nullptr;

        expr = std::make_unique<RangeExpr>(loc, std::move(expr), std::move(right), inclusive);
    }

    return expr;
}

ExprPtr Parser::parseCoalesce()
{
    ExprPtr expr = parseLogicalOr();
    if (!expr)
        return nullptr;

    while (match(TokenKind::QuestionQuestion))
    {
        SourceLoc loc = current_.loc;
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

    while (match(TokenKind::PipePipe))
    {
        SourceLoc loc = current_.loc;
        ExprPtr right = parseLogicalAnd();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::Or, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseLogicalAnd()
{
    ExprPtr expr = parseEquality();
    if (!expr)
        return nullptr;

    while (match(TokenKind::AmpAmp))
    {
        SourceLoc loc = current_.loc;
        ExprPtr right = parseEquality();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::And, std::move(expr), std::move(right));
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
        BinaryOp op = check(TokenKind::EqualEqual) ? BinaryOp::Eq : BinaryOp::Ne;
        SourceLoc loc = current_.loc;
        advance();

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
        BinaryOp op;
        if (check(TokenKind::Less))
            op = BinaryOp::Lt;
        else if (check(TokenKind::LessEqual))
            op = BinaryOp::Le;
        else if (check(TokenKind::Greater))
            op = BinaryOp::Gt;
        else
            op = BinaryOp::Ge;

        SourceLoc loc = current_.loc;
        advance();

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
        BinaryOp op = check(TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
        SourceLoc loc = current_.loc;
        advance();

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
        BinaryOp op;
        if (check(TokenKind::Star))
            op = BinaryOp::Mul;
        else if (check(TokenKind::Slash))
            op = BinaryOp::Div;
        else
            op = BinaryOp::Mod;

        SourceLoc loc = current_.loc;
        advance();

        ExprPtr right = parseUnary();
        if (!right)
            return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseUnary()
{
    if (check(TokenKind::Minus) || check(TokenKind::Bang) || check(TokenKind::Tilde))
    {
        UnaryOp op;
        if (check(TokenKind::Minus))
            op = UnaryOp::Neg;
        else if (check(TokenKind::Bang))
            op = UnaryOp::Not;
        else
            op = UnaryOp::BitNot;

        SourceLoc loc = current_.loc;
        advance();

        ExprPtr operand = parseUnary();
        if (!operand)
            return nullptr;

        return std::make_unique<UnaryExpr>(loc, op, std::move(operand));
    }

    return parsePostfix();
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

ExprPtr Parser::parseBinaryFrom(ExprPtr expr)
{
    // Parse multiplicative ops
    while (true)
    {
        if (match(TokenKind::Star))
        {
            SourceLoc loc = current_.loc;
            ExprPtr right = parseUnary();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Mul, std::move(expr), std::move(right));
        }
        else if (match(TokenKind::Slash))
        {
            SourceLoc loc = current_.loc;
            ExprPtr right = parseUnary();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Div, std::move(expr), std::move(right));
        }
        else if (match(TokenKind::Percent))
        {
            SourceLoc loc = current_.loc;
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
        if (match(TokenKind::Plus))
        {
            SourceLoc loc = current_.loc;
            ExprPtr right = parseMultiplicative();
            if (!right)
                return nullptr;
            expr =
                std::make_unique<BinaryExpr>(loc, BinaryOp::Add, std::move(expr), std::move(right));
        }
        else if (match(TokenKind::Minus))
        {
            SourceLoc loc = current_.loc;
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
        if (match(TokenKind::Less))
            op = BinaryOp::Lt;
        else if (match(TokenKind::LessEqual))
            op = BinaryOp::Le;
        else if (match(TokenKind::Greater))
            op = BinaryOp::Gt;
        else if (match(TokenKind::GreaterEqual))
            op = BinaryOp::Ge;
        else
            break;

        SourceLoc loc = current_.loc;
        ExprPtr right = parseAdditive();
        if (!right)
            return nullptr;
        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }
    // Parse equality ops
    while (true)
    {
        BinaryOp op;
        if (match(TokenKind::EqualEqual))
            op = BinaryOp::Eq;
        else if (match(TokenKind::NotEqual))
            op = BinaryOp::Ne;
        else
            break;

        SourceLoc loc = current_.loc;
        ExprPtr right = parseComparison();
        if (!right)
            return nullptr;
        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }
    // Parse logical and
    while (match(TokenKind::AmpAmp))
    {
        SourceLoc loc = current_.loc;
        ExprPtr right = parseEquality();
        if (!right)
            return nullptr;
        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::And, std::move(expr), std::move(right));
    }
    // Parse logical or
    while (match(TokenKind::PipePipe))
    {
        SourceLoc loc = current_.loc;
        ExprPtr right = parseLogicalAnd();
        if (!right)
            return nullptr;
        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::Or, std::move(expr), std::move(right));
    }
    return expr;
}

ExprPtr Parser::parsePostfixFrom(ExprPtr expr)
{
    while (true)
    {
        if (match(TokenKind::LParen))
        {
            // Function call
            SourceLoc loc = current_.loc;
            std::vector<CallArg> args = parseCallArgs();
            if (!expect(TokenKind::RParen, ")"))
                return nullptr;

            expr = std::make_unique<CallExpr>(loc, std::move(expr), std::move(args));
        }
        else if (match(TokenKind::LBracket))
        {
            // Index
            SourceLoc loc = current_.loc;
            ExprPtr index = parseExpression();
            if (!index)
                return nullptr;

            if (!expect(TokenKind::RBracket, "]"))
                return nullptr;

            expr = std::make_unique<IndexExpr>(loc, std::move(expr), std::move(index));
        }
        else if (match(TokenKind::Dot))
        {
            // Field access
            SourceLoc loc = current_.loc;
            if (!check(TokenKind::Identifier))
            {
                error("expected field name after '.'");
                return nullptr;
            }
            std::string field = current_.text;
            advance();

            expr = std::make_unique<FieldExpr>(loc, std::move(expr), std::move(field));
        }
        else if (match(TokenKind::QuestionDot))
        {
            // Optional chain
            SourceLoc loc = current_.loc;
            if (!check(TokenKind::Identifier))
            {
                error("expected field name after '?.'");
                return nullptr;
            }
            std::string field = current_.text;
            advance();

            expr = std::make_unique<OptionalChainExpr>(loc, std::move(expr), std::move(field));
        }
        else if (match(TokenKind::KwIs))
        {
            // Type check
            SourceLoc loc = current_.loc;
            TypePtr type = parseType();
            if (!type)
                return nullptr;

            expr = std::make_unique<IsExpr>(loc, std::move(expr), std::move(type));
        }
        else if (match(TokenKind::KwAs))
        {
            // Type cast
            SourceLoc loc = current_.loc;
            TypePtr type = parseType();
            if (!type)
                return nullptr;

            expr = std::make_unique<AsExpr>(loc, std::move(expr), std::move(type));
        }
        else if (match(TokenKind::Question))
        {
            // Try expression: expr? - propagate null/error
            // Note: This is different from optional type T? or ternary a ? b : c
            SourceLoc loc = current_.loc;
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

ExprPtr Parser::parsePrimary()
{
    SourceLoc loc = current_.loc;

    // Integer literal
    if (check(TokenKind::IntegerLiteral))
    {
        int64_t value = current_.intValue;
        advance();
        return std::make_unique<IntLiteralExpr>(loc, value);
    }

    // Number literal
    if (check(TokenKind::NumberLiteral))
    {
        double value = current_.floatValue;
        advance();
        return std::make_unique<NumberLiteralExpr>(loc, value);
    }

    // String literal
    if (check(TokenKind::StringLiteral))
    {
        std::string value = current_.stringValue;
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

    // Identifier
    if (check(TokenKind::Identifier))
    {
        std::string name = current_.text;
        advance();
        return std::make_unique<IdentExpr>(loc, std::move(name));
    }

    // Parenthesized expression or unit literal
    // Note: Lambda parsing is complex due to backtracking needs.
    // For now, only support () => expr syntax
    if (match(TokenKind::LParen))
    {
        // Check for unit literal () or lambda () => ...
        if (check(TokenKind::RParen))
        {
            advance(); // consume )
            // Check for lambda with no parameters: () => body
            if (match(TokenKind::Arrow))
            {
                ExprPtr body = parseExpression();
                if (!body)
                    return nullptr;
                return std::make_unique<LambdaExpr>(loc, std::vector<LambdaParam>{}, nullptr,
                                                    std::move(body));
            }
            return std::make_unique<UnitLiteralExpr>(loc);
        }

        // Regular parenthesized expression
        ExprPtr expr = parseExpression();
        if (!expr)
            return nullptr;

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        return expr;
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

ExprPtr Parser::parseListLiteral()
{
    SourceLoc loc = current_.loc;
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

ExprPtr Parser::parseInterpolatedString()
{
    SourceLoc loc = current_.loc;

    // First part: "text${
    std::string firstPart = current_.stringValue;
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
        std::string midPart = current_.stringValue;
        advance(); // consume StringMid

        // Concatenate the middle string part
        if (!midPart.empty())
        {
            result = std::make_unique<BinaryExpr>(
                loc, BinaryOp::Add, std::move(result),
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
        result = std::make_unique<BinaryExpr>(loc, BinaryOp::Add, std::move(result), std::move(expr));
    }

    // Must end with StringEnd
    if (!check(TokenKind::StringEnd))
    {
        error("expected end of interpolated string");
        return nullptr;
    }

    // Get the final string part
    std::string endPart = current_.stringValue;
    advance(); // consume StringEnd

    // Concatenate the final string part (if not empty)
    if (!endPart.empty())
    {
        result = std::make_unique<BinaryExpr>(
            loc, BinaryOp::Add, std::move(result),
            std::make_unique<StringLiteralExpr>(loc, std::move(endPart)));
    }

    return result;
}

ExprPtr Parser::parseMapOrSetLiteral()
{
    SourceLoc loc = current_.loc;
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
        if (check(TokenKind::Identifier))
        {
            // Look ahead for colon
            Token nameTok = current_;
            advance();

            if (match(TokenKind::Colon))
            {
                arg.name = nameTok.text;
                arg.value = parseExpression();
            }
            else
            {
                // Not a named arg, parse rest of expression starting with this identifier
                // We already consumed the identifier, so create an IdentExpr for it and
                // continue parsing from current_ position (which is after the identifier)
                ExprPtr ident = std::make_unique<IdentExpr>(nameTok.loc, nameTok.text);
                // Continue parsing using the binary expression parser with our ident as the LHS
                arg.value = parsePostfixAndBinaryFrom(std::move(ident));
            }
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

//===----------------------------------------------------------------------===//
// Statement Parsing
//===----------------------------------------------------------------------===//

StmtPtr Parser::parseStatement()
{
    SourceLoc loc = current_.loc;

    // Block
    if (check(TokenKind::LBrace))
    {
        return parseBlock();
    }

    // Java-style variable declaration: Type name = expr;
    // Detect by checking for Identifier followed by Identifier (or ? for optionals)
    // This handles: Integer x = 5; Person? p = ...; List[T] items = ...;
    if (check(TokenKind::Identifier))
    {
        // Look ahead to see if this is a Java-style declaration
        const Token &next = lexer_.peek();
        if (next.kind == TokenKind::Identifier)
        {
            // Simple type followed by variable name: Integer x = 5;
            return parseJavaStyleVarDecl();
        }
        else if (next.kind == TokenKind::Question)
        {
            // Optional type: Person? p = ...;
            // Type names are typically capitalized, so Person? identifier is a declaration
            // Try parsing as Java-style; parseType will consume Type?, then we expect identifier
            return parseJavaStyleVarDecl();
        }
        else if (next.kind == TokenKind::LBracket)
        {
            // Generic type: List[T] x = ...;
            // Parse as Java-style declaration
            return parseJavaStyleVarDecl();
        }
    }

    // If statement
    if (check(TokenKind::KwIf))
    {
        return parseIfStmt();
    }

    // While statement
    if (check(TokenKind::KwWhile))
    {
        return parseWhileStmt();
    }

    // For statement
    if (check(TokenKind::KwFor))
    {
        return parseForStmt();
    }

    // Return statement
    if (check(TokenKind::KwReturn))
    {
        return parseReturnStmt();
    }

    // Guard statement
    if (check(TokenKind::KwGuard))
    {
        return parseGuardStmt();
    }

    // Match statement
    if (check(TokenKind::KwMatch))
    {
        return parseMatchStmt();
    }

    // Break
    if (match(TokenKind::KwBreak))
    {
        if (!expect(TokenKind::Semicolon, ";"))
            return nullptr;
        return std::make_unique<BreakStmt>(loc);
    }

    // Continue
    if (match(TokenKind::KwContinue))
    {
        if (!expect(TokenKind::Semicolon, ";"))
            return nullptr;
        return std::make_unique<ContinueStmt>(loc);
    }

    // Expression statement
    ExprPtr expr = parseExpression();
    if (!expr)
        return nullptr;

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return std::make_unique<ExprStmt>(loc, std::move(expr));
}

StmtPtr Parser::parseBlock()
{
    SourceLoc loc = current_.loc;
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

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

    return std::make_unique<BlockStmt>(loc, std::move(statements));
}

StmtPtr Parser::parseVarDecl()
{
    SourceLoc loc = current_.loc;
    bool isFinal = check(TokenKind::KwFinal);
    advance(); // consume var/final

    if (!check(TokenKind::Identifier))
    {
        error("expected variable name");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    // Optional type annotation
    TypePtr type;
    if (match(TokenKind::Colon))
    {
        type = parseType();
        if (!type)
            return nullptr;
    }

    // Optional initializer
    ExprPtr init;
    if (match(TokenKind::Equal))
    {
        init = parseExpression();
        if (!init)
            return nullptr;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return std::make_unique<VarStmt>(
        loc, std::move(name), std::move(type), std::move(init), isFinal);
}

StmtPtr Parser::parseJavaStyleVarDecl()
{
    SourceLoc loc = current_.loc;

    // Parse the type (e.g., Integer, List[String], etc.)
    TypePtr type = parseType();
    if (!type)
        return nullptr;

    // Now we expect a variable name
    if (!check(TokenKind::Identifier))
    {
        error("expected variable name after type");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    // Optional initializer (= expr)
    ExprPtr init;
    if (match(TokenKind::Equal))
    {
        init = parseExpression();
        if (!init)
            return nullptr;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    // Java-style declarations are mutable by default (isFinal = false)
    return std::make_unique<VarStmt>(
        loc, std::move(name), std::move(type), std::move(init), false);
}

StmtPtr Parser::parseIfStmt()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'if'

    // ViperLang uses "if condition {" without parentheses
    ExprPtr condition = parseExpression();
    if (!condition)
        return nullptr;

    StmtPtr thenBranch = parseStatement();
    if (!thenBranch)
        return nullptr;

    StmtPtr elseBranch;
    if (match(TokenKind::KwElse))
    {
        elseBranch = parseStatement();
        if (!elseBranch)
            return nullptr;
    }

    return std::make_unique<IfStmt>(
        loc, std::move(condition), std::move(thenBranch), std::move(elseBranch));
}

StmtPtr Parser::parseWhileStmt()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'while'

    // ViperLang uses "while condition {" without parentheses
    ExprPtr condition = parseExpression();
    if (!condition)
        return nullptr;

    StmtPtr body = parseStatement();
    if (!body)
        return nullptr;

    return std::make_unique<WhileStmt>(loc, std::move(condition), std::move(body));
}

StmtPtr Parser::parseForStmt()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'for'

    if (!expect(TokenKind::LParen, "("))
        return nullptr;

    // Check for for-in loop: for (x in collection)
    if (check(TokenKind::Identifier))
    {
        std::string varName = current_.text;
        Token varTok = current_;
        advance();

        if (match(TokenKind::KwIn))
        {
            // For-in loop
            ExprPtr iterable = parseExpression();
            if (!iterable)
                return nullptr;

            if (!expect(TokenKind::RParen, ")"))
                return nullptr;

            StmtPtr body = parseStatement();
            if (!body)
                return nullptr;

            return std::make_unique<ForInStmt>(
                loc, std::move(varName), std::move(iterable), std::move(body));
        }

        // Not for-in, so we need to parse as regular for
        // Put back the identifier as part of the init
        // Actually, we consumed it, so let's just parse as expression starting with ident
        error("C-style for loops not yet implemented, use for-in");
        return nullptr;
    }

    error("expected variable name in for loop");
    return nullptr;
}

StmtPtr Parser::parseReturnStmt()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'return'

    ExprPtr value;
    if (!check(TokenKind::Semicolon))
    {
        value = parseExpression();
        if (!value)
            return nullptr;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return std::make_unique<ReturnStmt>(loc, std::move(value));
}

StmtPtr Parser::parseGuardStmt()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'guard'

    if (!expect(TokenKind::LParen, "("))
        return nullptr;

    ExprPtr condition = parseExpression();
    if (!condition)
        return nullptr;

    if (!expect(TokenKind::RParen, ")"))
        return nullptr;

    if (!expect(TokenKind::KwElse, "else"))
        return nullptr;

    StmtPtr elseBlock = parseStatement();
    if (!elseBlock)
        return nullptr;

    return std::make_unique<GuardStmt>(loc, std::move(condition), std::move(elseBlock));
}

StmtPtr Parser::parseMatchStmt()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'match'

    // Parse the scrutinee expression
    ExprPtr scrutinee = parseExpression();
    if (!scrutinee)
        return nullptr;

    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    std::vector<MatchArm> arms;

    // Parse match arms
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        MatchArm arm;

        // Parse pattern
        if (check(TokenKind::Identifier))
        {
            // Could be a binding or constructor
            std::string name = current_.text;
            advance();
            if (name == "_")
            {
                // Wildcard
                arm.pattern.kind = MatchArm::Pattern::Kind::Wildcard;
            }
            else
            {
                // Binding pattern (for now, treat identifiers as bindings)
                arm.pattern.kind = MatchArm::Pattern::Kind::Binding;
                arm.pattern.binding = name;
            }
        }
        else if (check(TokenKind::IntegerLiteral))
        {
            // Literal pattern
            arm.pattern.kind = MatchArm::Pattern::Kind::Literal;
            arm.pattern.literal = parsePrimary();
        }
        else if (check(TokenKind::StringLiteral))
        {
            // String literal pattern
            arm.pattern.kind = MatchArm::Pattern::Kind::Literal;
            arm.pattern.literal = parsePrimary();
        }
        else if (check(TokenKind::KwTrue) || check(TokenKind::KwFalse))
        {
            // Boolean literal pattern
            arm.pattern.kind = MatchArm::Pattern::Kind::Literal;
            arm.pattern.literal = parsePrimary();
        }
        else
        {
            error("expected pattern in match arm");
            return nullptr;
        }

        // Check for guard: 'where condition'
        // For now, skip guard parsing

        // Expect =>
        if (!expect(TokenKind::FatArrow, "=>"))
            return nullptr;

        // Parse arm body (expression or block)
        if (check(TokenKind::LBrace))
        {
            // Block body - parse as block expression
            SourceLoc blockLoc = current_.loc;
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

            arm.body = std::make_unique<BlockExpr>(blockLoc, std::move(statements), nullptr);
        }
        else
        {
            // Expression body
            arm.body = parseExpression();
            if (!arm.body)
                return nullptr;

            // Expect semicolon after expression body
            if (!expect(TokenKind::Semicolon, ";"))
                return nullptr;
        }

        arms.push_back(std::move(arm));
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return std::make_unique<MatchStmt>(loc, std::move(scrutinee), std::move(arms));
}

//===----------------------------------------------------------------------===//
// Type Parsing
//===----------------------------------------------------------------------===//

TypePtr Parser::parseType()
{
    TypePtr base = parseBaseType();
    if (!base)
        return nullptr;

    // Check for optional suffix ?
    while (match(TokenKind::Question))
    {
        base = std::make_unique<OptionalType>(base->loc, std::move(base));
    }

    return base;
}

TypePtr Parser::parseBaseType()
{
    SourceLoc loc = current_.loc;

    // Named type
    if (check(TokenKind::Identifier))
    {
        std::string name = current_.text;
        advance();

        // Check for generic parameters
        if (match(TokenKind::LBracket))
        {
            std::vector<TypePtr> args;

            do
            {
                TypePtr arg = parseType();
                if (!arg)
                    return nullptr;
                args.push_back(std::move(arg));
            } while (match(TokenKind::Comma));

            if (!expect(TokenKind::RBracket, "]"))
                return nullptr;

            return std::make_unique<GenericType>(loc, std::move(name), std::move(args));
        }

        return std::make_unique<NamedType>(loc, std::move(name));
    }

    // Tuple or function type: (A, B) or (A, B) -> C
    if (match(TokenKind::LParen))
    {
        std::vector<TypePtr> elements;

        if (!check(TokenKind::RParen))
        {
            do
            {
                TypePtr elem = parseType();
                if (!elem)
                    return nullptr;
                elements.push_back(std::move(elem));
            } while (match(TokenKind::Comma));
        }

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        // Check for function type
        if (match(TokenKind::Arrow))
        {
            TypePtr returnType = parseType();
            if (!returnType)
                return nullptr;

            return std::make_unique<FunctionType>(loc, std::move(elements), std::move(returnType));
        }

        // Tuple type
        return std::make_unique<TupleType>(loc, std::move(elements));
    }

    error("expected type");
    return nullptr;
}

//===----------------------------------------------------------------------===//
// Declaration Parsing
//===----------------------------------------------------------------------===//

std::unique_ptr<ModuleDecl> Parser::parseModule()
{
    SourceLoc loc = current_.loc;

    // module Name;
    if (!expect(TokenKind::KwModule, "module"))
        return nullptr;

    if (!check(TokenKind::Identifier))
    {
        error("expected module name");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    auto module = std::make_unique<ModuleDecl>(loc, std::move(name));

    // Parse imports
    while (check(TokenKind::KwImport))
    {
        module->imports.push_back(parseImportDecl());
    }

    // Parse declarations
    while (!check(TokenKind::Eof))
    {
        DeclPtr decl = parseDeclaration();
        if (!decl)
        {
            resyncAfterError();
            continue;
        }
        module->declarations.push_back(std::move(decl));
    }

    return module;
}

ImportDecl Parser::parseImportDecl()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'import'

    std::string path;

    // Check for string literal (file path import)
    if (check(TokenKind::StringLiteral))
    {
        // String token text includes quotes, so strip them
        std::string raw = current_.text;
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        {
            path = raw.substr(1, raw.size() - 2);
        }
        else
        {
            path = raw;
        }
        advance();
    }
    // Otherwise parse dotted identifier path: Viper.IO.File
    else if (check(TokenKind::Identifier))
    {
        path = current_.text;
        advance();

        while (match(TokenKind::Dot))
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected identifier in import path");
                return ImportDecl(loc, path);
            }
            path += ".";
            path += current_.text;
            advance();
        }
    }
    else
    {
        error("expected import path (string or identifier)");
        return ImportDecl(loc, "");
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return ImportDecl(loc, path);

    return ImportDecl(loc, path);
}

DeclPtr Parser::parseDeclaration()
{
    if (check(TokenKind::KwFunc))
    {
        return parseFunctionDecl();
    }
    if (check(TokenKind::KwValue))
    {
        return parseValueDecl();
    }
    if (check(TokenKind::KwEntity))
    {
        return parseEntityDecl();
    }
    if (check(TokenKind::KwInterface))
    {
        return parseInterfaceDecl();
    }
    // Module-level variable declarations (global variables)
    // Java-style: Integer x = 5; or legacy: var x = 5;
    if (check(TokenKind::Identifier))
    {
        // Look ahead to see if this is a Java-style declaration
        const Token &next = lexer_.peek();
        if (next.kind == TokenKind::Identifier)
        {
            // Simple type followed by variable name: Integer x = 5;
            return parseJavaStyleGlobalVarDecl();
        }
    }

    error("expected declaration");
    return nullptr;
}

DeclPtr Parser::parseFunctionDecl()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'func'

    if (!check(TokenKind::Identifier))
    {
        error("expected function name");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    auto func = std::make_unique<FunctionDecl>(loc, std::move(name));

    // Generic parameters
    func->genericParams = parseGenericParams();

    // Parameters
    if (!expect(TokenKind::LParen, "("))
        return nullptr;

    func->params = parseParameters();

    if (!expect(TokenKind::RParen, ")"))
        return nullptr;

    // Return type
    if (match(TokenKind::Arrow))
    {
        func->returnType = parseType();
        if (!func->returnType)
            return nullptr;
    }

    // Body
    if (check(TokenKind::LBrace))
    {
        func->body = parseBlock();
        if (!func->body)
            return nullptr;
    }
    else
    {
        error("expected function body");
        return nullptr;
    }

    return func;
}

std::vector<Param> Parser::parseParameters()
{
    std::vector<Param> params;

    if (check(TokenKind::RParen))
    {
        return params;
    }

    do
    {
        Param param;

        if (!check(TokenKind::Identifier))
        {
            error("expected parameter name");
            return {};
        }
        param.name = current_.text;
        advance();

        if (!expect(TokenKind::Colon, ":"))
            return {};

        param.type = parseType();
        if (!param.type)
            return {};

        // Default value
        if (match(TokenKind::Equal))
        {
            param.defaultValue = parseExpression();
            if (!param.defaultValue)
                return {};
        }

        params.push_back(std::move(param));
    } while (match(TokenKind::Comma));

    return params;
}

std::vector<std::string> Parser::parseGenericParams()
{
    std::vector<std::string> params;

    if (!match(TokenKind::LBracket))
    {
        return params;
    }

    do
    {
        if (!check(TokenKind::Identifier))
        {
            error("expected type parameter name");
            return {};
        }
        params.push_back(current_.text);
        advance();
    } while (match(TokenKind::Comma));

    if (!expect(TokenKind::RBracket, "]"))
        return {};

    return params;
}

DeclPtr Parser::parseValueDecl()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'value'

    if (!check(TokenKind::Identifier))
    {
        error("expected value type name");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    auto value = std::make_unique<ValueDecl>(loc, std::move(name));

    // Generic parameters
    value->genericParams = parseGenericParams();

    // Implements clause
    if (match(TokenKind::KwImplements))
    {
        do
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected interface name");
                return nullptr;
            }
            value->interfaces.push_back(current_.text);
            advance();
        } while (match(TokenKind::Comma));
    }

    // Body
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    // Parse members (fields and methods)
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        if (check(TokenKind::KwFunc))
        {
            // Method declaration
            auto method = parseMethodDecl();
            if (method)
            {
                value->members.push_back(std::move(method));
            }
        }
        else if (check(TokenKind::Identifier))
        {
            // Field declaration: TypeName fieldName;
            auto field = parseFieldDecl();
            if (field)
            {
                value->members.push_back(std::move(field));
            }
        }
        else
        {
            error("expected field or method declaration");
            advance();
        }
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return value;
}

DeclPtr Parser::parseEntityDecl()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'entity'

    if (!check(TokenKind::Identifier))
    {
        error("expected entity type name");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    auto entity = std::make_unique<EntityDecl>(loc, std::move(name));

    // Generic parameters
    entity->genericParams = parseGenericParams();

    // Extends clause
    if (match(TokenKind::KwExtends))
    {
        if (!check(TokenKind::Identifier))
        {
            error("expected base class name");
            return nullptr;
        }
        entity->baseClass = current_.text;
        advance();
    }

    // Implements clause
    if (match(TokenKind::KwImplements))
    {
        do
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected interface name");
                return nullptr;
            }
            entity->interfaces.push_back(current_.text);
            advance();
        } while (match(TokenKind::Comma));
    }

    // Body
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    // Parse members (fields and methods)
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        if (check(TokenKind::KwFunc))
        {
            // Method declaration
            auto method = parseMethodDecl();
            if (method)
            {
                entity->members.push_back(std::move(method));
            }
        }
        else if (check(TokenKind::Identifier))
        {
            // Field declaration: TypeName fieldName;
            auto field = parseFieldDecl();
            if (field)
            {
                entity->members.push_back(std::move(field));
            }
        }
        else
        {
            error("expected field or method declaration");
            advance();
        }
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return entity;
}

DeclPtr Parser::parseInterfaceDecl()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'interface'

    if (!check(TokenKind::Identifier))
    {
        error("expected interface name");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    auto iface = std::make_unique<InterfaceDecl>(loc, std::move(name));

    // Generic parameters
    iface->genericParams = parseGenericParams();

    // Body
    if (!expect(TokenKind::LBrace, "{"))
        return nullptr;

    // Parse method signatures
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        if (check(TokenKind::KwFunc))
        {
            // Parse method signature (method without body)
            auto method = parseMethodDecl();
            if (method)
            {
                iface->members.push_back(std::move(method));
            }
        }
        else
        {
            error("expected method signature in interface");
            advance();
        }
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return iface;
}

DeclPtr Parser::parseGlobalVarDecl()
{
    SourceLoc loc = current_.loc;
    bool isFinal = check(TokenKind::KwFinal);
    advance(); // consume 'var' or 'final'

    if (!check(TokenKind::Identifier))
    {
        error("expected variable name");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    auto decl = std::make_unique<GlobalVarDecl>(loc, std::move(name));
    decl->isFinal = isFinal;

    // Optional type annotation: var x: Integer
    if (match(TokenKind::Colon))
    {
        decl->type = parseType();
        if (!decl->type)
            return nullptr;
    }

    // Optional initializer: var x = 42
    if (match(TokenKind::Equal))
    {
        decl->initializer = parseExpression();
        if (!decl->initializer)
            return nullptr;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return decl;
}

DeclPtr Parser::parseJavaStyleGlobalVarDecl()
{
    SourceLoc loc = current_.loc;

    // Parse the type (e.g., Integer, List, etc.)
    TypePtr type = parseType();
    if (!type)
        return nullptr;

    // Now we expect a variable name
    if (!check(TokenKind::Identifier))
    {
        error("expected variable name after type");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    auto decl = std::make_unique<GlobalVarDecl>(loc, std::move(name));
    decl->type = std::move(type);
    decl->isFinal = false; // Java-style declarations are mutable by default

    // Optional initializer: Integer x = 42
    if (match(TokenKind::Equal))
    {
        decl->initializer = parseExpression();
        if (!decl->initializer)
            return nullptr;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return decl;
}

DeclPtr Parser::parseFieldDecl()
{
    SourceLoc loc = current_.loc;

    // Type name (e.g., Integer, String, Point)
    if (!check(TokenKind::Identifier))
    {
        error("expected type name");
        return nullptr;
    }
    std::string typeName = current_.text;
    advance();

    TypePtr type = std::make_unique<NamedType>(loc, typeName);

    // Field name
    if (!check(TokenKind::Identifier))
    {
        error("expected field name");
        return nullptr;
    }
    std::string fieldName = current_.text;
    advance();

    auto field = std::make_unique<FieldDecl>(loc, std::move(fieldName));
    field->type = std::move(type);

    // Optional initializer: = expr
    if (match(TokenKind::Equal))
    {
        field->initializer = parseExpression();
    }

    // Expect semicolon
    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return field;
}

DeclPtr Parser::parseMethodDecl()
{
    SourceLoc loc = current_.loc;
    advance(); // consume 'func'

    if (!check(TokenKind::Identifier))
    {
        error("expected method name");
        return nullptr;
    }
    std::string name = current_.text;
    advance();

    auto method = std::make_unique<MethodDecl>(loc, std::move(name));

    // Generic parameters
    method->genericParams = parseGenericParams();

    // Parameters
    if (!expect(TokenKind::LParen, "("))
        return nullptr;
    method->params = parseParameters();
    if (!expect(TokenKind::RParen, ")"))
        return nullptr;

    // Return type
    if (match(TokenKind::Arrow))
    {
        method->returnType = parseType();
    }

    // Body
    if (check(TokenKind::LBrace))
    {
        method->body = parseBlock();
    }
    else
    {
        // No body - interface method signature
        if (!expect(TokenKind::Semicolon, ";"))
            return nullptr;
    }

    return method;
}

} // namespace il::frontends::viperlang
