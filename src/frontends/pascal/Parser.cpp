//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Parser.cpp
// Purpose: Implements the recursive descent parser for Viper Pascal.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Parser.hpp"

namespace il::frontends::pascal
{

//=============================================================================
// Constructor
//=============================================================================

Parser::Parser(Lexer &lexer, il::support::DiagnosticEngine &diag)
    : lexer_(lexer), diag_(diag)
{
    // Prime the parser with the first token
    current_ = lexer_.next();
}

//=============================================================================
// Token Handling
//=============================================================================

const Token &Parser::peek() const
{
    return current_;
}

Token Parser::advance()
{
    Token result = current_;
    current_ = lexer_.next();
    return result;
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
    // Skip tokens until we hit a synchronization point
    while (!check(TokenKind::Eof))
    {
        // Synchronize on statement terminators and block keywords
        if (check(TokenKind::Semicolon) ||
            check(TokenKind::KwEnd) ||
            check(TokenKind::KwElse) ||
            check(TokenKind::KwUntil))
        {
            return;
        }
        advance();
    }
}

//=============================================================================
// Token Utilities
//=============================================================================

bool Parser::isKeyword(TokenKind kind)
{
    // Check if token is in the keyword range (KwAnd through KwFinalization)
    return kind >= TokenKind::KwAnd && kind <= TokenKind::KwFinalization;
}

//=============================================================================
// Error Handling
//=============================================================================

void Parser::error(const std::string &message)
{
    errorAt(current_.loc, message);
}

void Parser::errorAt(il::support::SourceLoc loc, const std::string &message)
{
    hasError_ = true;
    diag_.report(il::support::Diagnostic{il::support::Severity::Error, message, loc, ""});
}

//=============================================================================
// Expression Parsing
//=============================================================================

std::unique_ptr<Expr> Parser::parseExpression()
{
    return parseCoalesce();
}

// Coalesce: relation { "??" relation } (left-associative, lowest precedence)
std::unique_ptr<Expr> Parser::parseCoalesce()
{
    auto left = parseRelation();
    if (!left)
        return nullptr;

    while (match(TokenKind::NilCoalesce))
    {
        auto loc = current_.loc;
        auto right = parseRelation();
        if (!right)
            return nullptr;
        left = std::make_unique<BinaryExpr>(
            BinaryExpr::Op::Coalesce, std::move(left), std::move(right), loc);
    }

    return left;
}

// Relation: simple [relop simple]
std::unique_ptr<Expr> Parser::parseRelation()
{
    auto left = parseSimple();
    if (!left)
        return nullptr;

    // Check for relational operators
    BinaryExpr::Op op;
    bool hasOp = false;

    if (check(TokenKind::Equal))
    {
        op = BinaryExpr::Op::Eq;
        hasOp = true;
    }
    else if (check(TokenKind::NotEqual))
    {
        op = BinaryExpr::Op::Ne;
        hasOp = true;
    }
    else if (check(TokenKind::Less))
    {
        op = BinaryExpr::Op::Lt;
        hasOp = true;
    }
    else if (check(TokenKind::Greater))
    {
        op = BinaryExpr::Op::Gt;
        hasOp = true;
    }
    else if (check(TokenKind::LessEqual))
    {
        op = BinaryExpr::Op::Le;
        hasOp = true;
    }
    else if (check(TokenKind::GreaterEqual))
    {
        op = BinaryExpr::Op::Ge;
        hasOp = true;
    }
    else if (check(TokenKind::KwIn))
    {
        op = BinaryExpr::Op::In;
        hasOp = true;
    }

    if (hasOp)
    {
        auto loc = current_.loc;
        advance();
        auto right = parseSimple();
        if (!right)
            return nullptr;
        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), loc);
    }

    return left;
}

// Simple: ["-" | "+"] term { addop term }
std::unique_ptr<Expr> Parser::parseSimple()
{
    // Handle leading unary +/-
    // Note: unary - applies only to the immediately following factor, not term
    // So "-x * y" should parse as "(-x) * y"
    // This is handled in parseFactor() for not, and here for +/-

    std::unique_ptr<Expr> left = parseTerm();
    if (!left)
        return nullptr;

    // Handle additive operators (including or)
    while (true)
    {
        BinaryExpr::Op op;
        if (check(TokenKind::Plus))
        {
            op = BinaryExpr::Op::Add;
        }
        else if (check(TokenKind::Minus))
        {
            op = BinaryExpr::Op::Sub;
        }
        else if (check(TokenKind::KwOr))
        {
            op = BinaryExpr::Op::Or;
        }
        else
        {
            break;
        }

        auto loc = current_.loc;
        advance();
        auto right = parseTerm();
        if (!right)
            return nullptr;
        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), loc);
    }

    return left;
}

// Term: factor { mulop factor }
std::unique_ptr<Expr> Parser::parseTerm()
{
    auto left = parseFactor();
    if (!left)
        return nullptr;

    while (true)
    {
        BinaryExpr::Op op;
        if (check(TokenKind::Star))
        {
            op = BinaryExpr::Op::Mul;
        }
        else if (check(TokenKind::Slash))
        {
            op = BinaryExpr::Op::Div;
        }
        else if (check(TokenKind::KwDiv))
        {
            op = BinaryExpr::Op::IntDiv;
        }
        else if (check(TokenKind::KwMod))
        {
            op = BinaryExpr::Op::Mod;
        }
        else if (check(TokenKind::KwAnd))
        {
            op = BinaryExpr::Op::And;
        }
        else
        {
            break;
        }

        auto loc = current_.loc;
        advance();
        auto right = parseFactor();
        if (!right)
            return nullptr;
        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right), loc);
    }

    return left;
}

// Factor: "not" factor | ["+"|"-"] factor | primary
std::unique_ptr<Expr> Parser::parseFactor()
{
    // Handle unary operators: not, +, -
    if (check(TokenKind::KwNot))
    {
        auto loc = current_.loc;
        advance();
        auto operand = parseFactor();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Not, std::move(operand), loc);
    }

    if (check(TokenKind::Minus))
    {
        auto loc = current_.loc;
        advance();
        auto operand = parseFactor();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Neg, std::move(operand), loc);
    }

    if (check(TokenKind::Plus))
    {
        auto loc = current_.loc;
        advance();
        auto operand = parseFactor();
        if (!operand)
            return nullptr;
        return std::make_unique<UnaryExpr>(UnaryExpr::Op::Plus, std::move(operand), loc);
    }

    return parsePrimary();
}

// Primary: literal | designator | "(" expr ")" | "@" factor | set-constructor
std::unique_ptr<Expr> Parser::parsePrimary()
{
    auto loc = current_.loc;

    // Integer literal
    if (check(TokenKind::IntegerLiteral))
    {
        auto value = current_.intValue;
        advance();
        return std::make_unique<IntLiteralExpr>(value, loc);
    }

    // Real literal
    if (check(TokenKind::RealLiteral))
    {
        auto value = current_.realValue;
        advance();
        return std::make_unique<RealLiteralExpr>(value, loc);
    }

    // String literal
    if (check(TokenKind::StringLiteral))
    {
        auto value = current_.canonical; // canonical contains the actual string content
        advance();
        return std::make_unique<StringLiteralExpr>(std::move(value), loc);
    }

    // Nil literal
    if (check(TokenKind::KwNil))
    {
        advance();
        return std::make_unique<NilLiteralExpr>(loc);
    }

    // Boolean literals (True/False are predefined identifiers)
    if (check(TokenKind::Identifier) && current_.isPredefined)
    {
        if (current_.canonical == "true")
        {
            advance();
            return std::make_unique<BoolLiteralExpr>(true, loc);
        }
        if (current_.canonical == "false")
        {
            advance();
            return std::make_unique<BoolLiteralExpr>(false, loc);
        }
    }

    // Identifier (designator)
    if (check(TokenKind::Identifier))
    {
        return parseDesignator();
    }

    // Parenthesized expression
    if (check(TokenKind::LParen))
    {
        advance();
        auto expr = parseExpression();
        if (!expr)
            return nullptr;
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
        return expr;
    }

    // Address-of operator
    if (check(TokenKind::At))
    {
        advance();
        auto operand = parseFactor();
        if (!operand)
            return nullptr;
        return std::make_unique<AddressOfExpr>(std::move(operand), loc);
    }

    // Set constructor [...]
    if (check(TokenKind::LBracket))
    {
        advance();
        std::vector<SetConstructorExpr::Element> elements;

        if (!check(TokenKind::RBracket))
        {
            do
            {
                auto start = parseExpression();
                if (!start)
                    return nullptr;

                SetConstructorExpr::Element elem;
                elem.start = std::move(start);

                // Check for range ..
                if (match(TokenKind::DotDot))
                {
                    elem.end = parseExpression();
                    if (!elem.end)
                        return nullptr;
                }

                elements.push_back(std::move(elem));
            } while (match(TokenKind::Comma));
        }

        if (!expect(TokenKind::RBracket, "']'"))
            return nullptr;

        return std::make_unique<SetConstructorExpr>(std::move(elements), loc);
    }

    error("expected expression");
    return nullptr;
}

// Designator: identifier { "." ident | "[" exprList "]" | "(" argList ")" | "^" }
std::unique_ptr<Expr> Parser::parseDesignator()
{
    auto loc = current_.loc;

    if (!check(TokenKind::Identifier))
    {
        error("expected identifier");
        return nullptr;
    }

    auto name = current_.text;
    advance();

    std::unique_ptr<Expr> base = std::make_unique<NameExpr>(std::move(name), loc);

    return parseDesignatorSuffix(std::move(base));
}

std::unique_ptr<Expr> Parser::parseDesignatorSuffix(std::unique_ptr<Expr> base)
{
    while (true)
    {
        auto loc = current_.loc;

        // Field access: .ident
        if (match(TokenKind::Dot))
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected field name after '.'");
                return nullptr;
            }
            auto field = current_.text;
            advance();
            base = std::make_unique<FieldExpr>(std::move(base), std::move(field), loc);
            continue;
        }

        // Index access: [exprList]
        if (match(TokenKind::LBracket))
        {
            auto indices = parseExprList();
            if (indices.empty() && hasError_)
                return nullptr;
            if (!expect(TokenKind::RBracket, "']'"))
                return nullptr;
            base = std::make_unique<IndexExpr>(std::move(base), std::move(indices), loc);
            continue;
        }

        // Call: (argList)
        if (match(TokenKind::LParen))
        {
            std::vector<std::unique_ptr<Expr>> args;
            if (!check(TokenKind::RParen))
            {
                args = parseExprList();
                if (args.empty() && hasError_)
                    return nullptr;
            }
            if (!expect(TokenKind::RParen, "')'"))
                return nullptr;
            base = std::make_unique<CallExpr>(std::move(base), std::move(args), loc);
            continue;
        }

        // Dereference: ^
        if (match(TokenKind::Caret))
        {
            base = std::make_unique<DereferenceExpr>(std::move(base), loc);
            continue;
        }

        // No more suffixes
        break;
    }

    return base;
}

std::vector<std::unique_ptr<Expr>> Parser::parseExprList()
{
    std::vector<std::unique_ptr<Expr>> result;

    auto expr = parseExpression();
    if (!expr)
        return result;
    result.push_back(std::move(expr));

    while (match(TokenKind::Comma))
    {
        expr = parseExpression();
        if (!expr)
            return result;
        result.push_back(std::move(expr));
    }

    return result;
}

//=============================================================================
// Statement Parsing
//=============================================================================

std::unique_ptr<Stmt> Parser::parseStatement()
{
    auto loc = current_.loc;

    // Empty statement (just semicolon or end of block)
    if (check(TokenKind::Semicolon) ||
        check(TokenKind::KwEnd) ||
        check(TokenKind::KwElse) ||
        check(TokenKind::KwUntil))
    {
        return std::make_unique<EmptyStmt>(loc);
    }

    // If statement
    if (check(TokenKind::KwIf))
    {
        return parseIf();
    }

    // While statement
    if (check(TokenKind::KwWhile))
    {
        return parseWhile();
    }

    // Repeat statement
    if (check(TokenKind::KwRepeat))
    {
        return parseRepeat();
    }

    // For statement
    if (check(TokenKind::KwFor))
    {
        return parseFor();
    }

    // Case statement
    if (check(TokenKind::KwCase))
    {
        return parseCase();
    }

    // Begin-end block
    if (check(TokenKind::KwBegin))
    {
        return parseBlock();
    }

    // Break statement
    if (check(TokenKind::KwBreak))
    {
        advance();
        return std::make_unique<BreakStmt>(loc);
    }

    // Continue statement
    if (check(TokenKind::KwContinue))
    {
        advance();
        return std::make_unique<ContinueStmt>(loc);
    }

    // Inherited statement: inherited; or inherited MethodName(args);
    if (check(TokenKind::KwInherited))
    {
        advance();
        std::string methodName;
        std::vector<std::unique_ptr<Expr>> args;

        // Check if there's a method name
        if (check(TokenKind::Identifier))
        {
            methodName = current_.text;
            advance();

            // Parse arguments if present
            if (match(TokenKind::LParen))
            {
                if (!check(TokenKind::RParen))
                {
                    do
                    {
                        auto arg = parseExpression();
                        if (!arg)
                            return nullptr;
                        args.push_back(std::move(arg));
                    } while (match(TokenKind::Comma));
                }
                if (!expect(TokenKind::RParen, "')'"))
                    return nullptr;
            }
        }

        return std::make_unique<InheritedStmt>(std::move(methodName), std::move(args), loc);
    }

    // Raise statement
    if (check(TokenKind::KwRaise))
    {
        return parseRaise();
    }

    // Try statement (try-except or try-finally)
    if (check(TokenKind::KwTry))
    {
        return parseTry();
    }

    // Assignment or call statement (starts with designator)
    if (check(TokenKind::Identifier))
    {
        auto expr = parseDesignator();
        if (!expr)
            return nullptr;

        // Check for assignment
        if (match(TokenKind::Assign))
        {
            auto value = parseExpression();
            if (!value)
                return nullptr;
            return std::make_unique<AssignStmt>(std::move(expr), std::move(value), loc);
        }

        // Must be a call statement (expr should be a CallExpr or we wrap it)
        if (expr->kind == ExprKind::Call)
        {
            return std::make_unique<CallStmt>(std::move(expr), loc);
        }

        // If it's just an identifier, it could be a procedure call without parens
        // Create a CallExpr with no arguments
        if (expr->kind == ExprKind::Name || expr->kind == ExprKind::Field)
        {
            std::vector<std::unique_ptr<Expr>> args;
            auto call = std::make_unique<CallExpr>(std::move(expr), std::move(args), loc);
            return std::make_unique<CallStmt>(std::move(call), loc);
        }

        error("expected assignment or procedure call");
        return nullptr;
    }

    error("expected statement");
    return nullptr;
}

std::unique_ptr<Stmt> Parser::parseIf()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwIf, "'if'"))
        return nullptr;

    auto condition = parseExpression();
    if (!condition)
        return nullptr;

    if (!expect(TokenKind::KwThen, "'then'"))
        return nullptr;

    auto thenBranch = parseStatement();
    if (!thenBranch)
        return nullptr;

    std::unique_ptr<Stmt> elseBranch;
    if (match(TokenKind::KwElse))
    {
        elseBranch = parseStatement();
        if (!elseBranch)
            return nullptr;
    }

    return std::make_unique<IfStmt>(
        std::move(condition), std::move(thenBranch), std::move(elseBranch), loc);
}

std::unique_ptr<Stmt> Parser::parseWhile()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwWhile, "'while'"))
        return nullptr;

    auto condition = parseExpression();
    if (!condition)
        return nullptr;

    if (!expect(TokenKind::KwDo, "'do'"))
        return nullptr;

    auto body = parseStatement();
    if (!body)
        return nullptr;

    return std::make_unique<WhileStmt>(std::move(condition), std::move(body), loc);
}

std::unique_ptr<Stmt> Parser::parseRepeat()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwRepeat, "'repeat'"))
        return nullptr;

    // Parse statement list (not a block - no begin/end)
    auto stmts = parseStatementList();

    if (!expect(TokenKind::KwUntil, "'until'"))
        return nullptr;

    auto condition = parseExpression();
    if (!condition)
        return nullptr;

    // Wrap statements in a block
    auto body = std::make_unique<BlockStmt>(std::move(stmts), loc);

    return std::make_unique<RepeatStmt>(std::move(body), std::move(condition), loc);
}

std::unique_ptr<Stmt> Parser::parseFor()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwFor, "'for'"))
        return nullptr;

    if (!check(TokenKind::Identifier))
    {
        error("expected loop variable");
        return nullptr;
    }

    auto loopVar = current_.text;
    advance();

    // Check for := (standard for) or in (for-in)
    if (match(TokenKind::Assign))
    {
        // Standard for loop: for i := start to/downto end do body
        auto start = parseExpression();
        if (!start)
            return nullptr;

        ForDirection direction;
        if (match(TokenKind::KwTo))
        {
            direction = ForDirection::To;
        }
        else if (match(TokenKind::KwDownto))
        {
            direction = ForDirection::Downto;
        }
        else
        {
            error("expected 'to' or 'downto'");
            return nullptr;
        }

        auto bound = parseExpression();
        if (!bound)
            return nullptr;

        if (!expect(TokenKind::KwDo, "'do'"))
            return nullptr;

        auto body = parseStatement();
        if (!body)
            return nullptr;

        return std::make_unique<ForStmt>(
            std::move(loopVar), std::move(start), std::move(bound), direction, std::move(body), loc);
    }
    else if (match(TokenKind::KwIn))
    {
        // For-in loop: for item in collection do body
        auto collection = parseExpression();
        if (!collection)
            return nullptr;

        if (!expect(TokenKind::KwDo, "'do'"))
            return nullptr;

        auto body = parseStatement();
        if (!body)
            return nullptr;

        return std::make_unique<ForInStmt>(
            std::move(loopVar), std::move(collection), std::move(body), loc);
    }
    else
    {
        error("expected ':=' or 'in' after loop variable");
        return nullptr;
    }
}

std::unique_ptr<Stmt> Parser::parseCase()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwCase, "'case'"))
        return nullptr;

    auto expr = parseExpression();
    if (!expr)
        return nullptr;

    if (!expect(TokenKind::KwOf, "'of'"))
        return nullptr;

    std::vector<CaseArm> arms;

    // Parse case arms: label-list : statement
    // Continue while we don't see 'end' or 'else'
    while (!check(TokenKind::KwEnd) && !check(TokenKind::KwElse) && !check(TokenKind::Eof))
    {
        CaseArm arm;
        arm.loc = current_.loc;

        // Parse label list (comma-separated expressions)
        // Note: we do NOT allow ranges here (1..5) - just individual values
        auto label = parseExpression();
        if (!label)
            return nullptr;
        arm.labels.push_back(std::move(label));

        while (match(TokenKind::Comma))
        {
            label = parseExpression();
            if (!label)
                return nullptr;
            arm.labels.push_back(std::move(label));
        }

        if (!expect(TokenKind::Colon, "':'"))
            return nullptr;

        arm.body = parseStatement();
        if (!arm.body)
            return nullptr;

        arms.push_back(std::move(arm));

        // Consume optional semicolon after case arm
        match(TokenKind::Semicolon);
    }

    // Parse optional else clause
    std::unique_ptr<Stmt> elseBody;
    if (match(TokenKind::KwElse))
    {
        std::vector<std::unique_ptr<Stmt>> elseStmts;
        while (!check(TokenKind::KwEnd) && !check(TokenKind::Eof))
        {
            auto stmt = parseStatement();
            if (stmt)
                elseStmts.push_back(std::move(stmt));
            while (match(TokenKind::Semicolon))
            {
            }
        }
        elseBody = std::make_unique<BlockStmt>(std::move(elseStmts), loc);
    }

    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    return std::make_unique<CaseStmt>(std::move(expr), std::move(arms), std::move(elseBody), loc);
}

std::unique_ptr<BlockStmt> Parser::parseBlock()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwBegin, "'begin'"))
        return nullptr;

    auto stmts = parseStatementList();

    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    return std::make_unique<BlockStmt>(std::move(stmts), loc);
}

std::vector<std::unique_ptr<Stmt>> Parser::parseStatementList()
{
    std::vector<std::unique_ptr<Stmt>> result;

    // Parse first statement
    auto stmt = parseStatement();
    if (stmt)
        result.push_back(std::move(stmt));

    // Parse remaining statements separated by semicolons
    while (match(TokenKind::Semicolon))
    {
        // Don't parse another statement if we're at end/until/else
        if (check(TokenKind::KwEnd) ||
            check(TokenKind::KwUntil) ||
            check(TokenKind::KwElse))
        {
            break;
        }

        stmt = parseStatement();
        if (stmt)
            result.push_back(std::move(stmt));
    }

    return result;
}

std::unique_ptr<Stmt> Parser::parseRaise()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwRaise, "'raise'"))
        return nullptr;

    // Check if there's an exception expression (not just re-raise)
    std::unique_ptr<Expr> exception;
    if (!check(TokenKind::Semicolon) &&
        !check(TokenKind::KwEnd) &&
        !check(TokenKind::KwElse))
    {
        exception = parseExpression();
        // It's okay if expression fails - could be just "raise;"
    }

    return std::make_unique<RaiseStmt>(std::move(exception), loc);
}

std::unique_ptr<Stmt> Parser::parseTry()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwTry, "'try'"))
        return nullptr;

    // Parse try body statements
    std::vector<std::unique_ptr<Stmt>> tryStmts;
    while (!check(TokenKind::KwExcept) &&
           !check(TokenKind::KwFinally) &&
           !check(TokenKind::KwEnd) &&
           !check(TokenKind::Eof))
    {
        auto stmt = parseStatement();
        if (stmt)
            tryStmts.push_back(std::move(stmt));

        // Consume optional semicolons between statements
        while (match(TokenKind::Semicolon))
        {
        }
    }

    auto tryBody = std::make_unique<BlockStmt>(std::move(tryStmts), loc);

    // Check for except or finally
    if (match(TokenKind::KwExcept))
    {
        // try-except statement
        std::vector<ExceptHandler> handlers;

        // Parse exception handlers: on E: Type do ...
        while (check(TokenKind::KwOn))
        {
            advance(); // consume 'on'
            auto handlerLoc = current_.loc;

            // Parse optional variable name
            std::string varName;
            std::string typeName;

            if (!check(TokenKind::Identifier))
            {
                error("expected identifier after 'on'");
                return nullptr;
            }

            // Could be "on E: Type" or "on Type" (no variable)
            Token first = advance();
            if (match(TokenKind::Colon))
            {
                // Form: on E: Type
                varName = first.text;
                if (!check(TokenKind::Identifier))
                {
                    error("expected type name after ':'");
                    return nullptr;
                }
                typeName = advance().text;
            }
            else
            {
                // Form: on Type (no variable binding)
                typeName = first.text;
            }

            if (!expect(TokenKind::KwDo, "'do'"))
                return nullptr;

            auto handlerBody = parseStatement();
            if (!handlerBody)
                return nullptr;

            handlers.push_back({varName, typeName, std::move(handlerBody), handlerLoc});

            // Consume optional semicolon after handler
            match(TokenKind::Semicolon);
        }

        // Parse optional else clause
        std::unique_ptr<Stmt> elseBody;
        if (match(TokenKind::KwElse))
        {
            std::vector<std::unique_ptr<Stmt>> elseStmts;
            while (!check(TokenKind::KwEnd) && !check(TokenKind::Eof))
            {
                auto stmt = parseStatement();
                if (stmt)
                    elseStmts.push_back(std::move(stmt));
                while (match(TokenKind::Semicolon))
                {
                }
            }
            elseBody = std::make_unique<BlockStmt>(std::move(elseStmts), loc);
        }

        if (!expect(TokenKind::KwEnd, "'end'"))
            return nullptr;

        return std::make_unique<TryExceptStmt>(
            std::move(tryBody), std::move(handlers), std::move(elseBody), loc);
    }
    else if (match(TokenKind::KwFinally))
    {
        // try-finally statement
        std::vector<std::unique_ptr<Stmt>> finallyStmts;
        while (!check(TokenKind::KwEnd) && !check(TokenKind::Eof))
        {
            auto stmt = parseStatement();
            if (stmt)
                finallyStmts.push_back(std::move(stmt));

            while (match(TokenKind::Semicolon))
            {
            }
        }

        auto finallyBody = std::make_unique<BlockStmt>(std::move(finallyStmts), loc);

        if (!expect(TokenKind::KwEnd, "'end'"))
            return nullptr;

        return std::make_unique<TryFinallyStmt>(std::move(tryBody), std::move(finallyBody), loc);
    }
    else
    {
        error("expected 'except' or 'finally' after try block");
        return nullptr;
    }
}

//=============================================================================
// Type Parsing
//=============================================================================

std::unique_ptr<TypeNode> Parser::parseType()
{
    auto type = parseBaseType();
    if (!type)
        return nullptr;

    // Check for optional suffix (?)
    if (match(TokenKind::Question))
    {
        auto loc = current_.loc;
        type = std::make_unique<OptionalTypeNode>(std::move(type), loc);

        // Reject T?? (double optional) - second ? could be another Question
        // or could be consumed as part of a ?? (NilCoalesce) token by the lexer
        if (check(TokenKind::Question) || check(TokenKind::NilCoalesce))
        {
            error("double optional type is not allowed");
            return nullptr;
        }
    }
    else if (check(TokenKind::NilCoalesce))
    {
        // T?? written without space - lexer tokenized as NilCoalesce
        error("double optional type is not allowed");
        return nullptr;
    }

    return type;
}

std::unique_ptr<TypeNode> Parser::parseBaseType()
{
    auto loc = current_.loc;

    // Array type
    if (check(TokenKind::KwArray))
    {
        return parseArrayType();
    }

    // Record type
    if (check(TokenKind::KwRecord))
    {
        return parseRecordType();
    }

    // Set type
    if (check(TokenKind::KwSet))
    {
        return parseSetType();
    }

    // Pointer type
    if (check(TokenKind::Caret))
    {
        return parsePointerType();
    }

    // Procedure type
    if (check(TokenKind::KwProcedure))
    {
        return parseProcedureType();
    }

    // Function type
    if (check(TokenKind::KwFunction))
    {
        return parseFunctionType();
    }

    // Enumeration type (starts with open paren containing identifiers)
    if (check(TokenKind::LParen))
    {
        return parseEnumType();
    }

    // Named type (identifier)
    if (check(TokenKind::Identifier))
    {
        auto name = current_.text;
        advance();
        return std::make_unique<NamedTypeNode>(std::move(name), loc);
    }

    error("expected type");
    return nullptr;
}

std::unique_ptr<TypeNode> Parser::parseArrayType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwArray, "'array'"))
        return nullptr;

    std::vector<ArrayTypeNode::DimRange> dimensions;

    // Check for dimension specification [...]
    if (match(TokenKind::LBracket))
    {
        // Parse dimensions (comma-separated)
        do
        {
            ArrayTypeNode::DimRange dim;

            // Parse dimension - could be a single size or a range
            auto expr = parseExpression();
            if (!expr)
                return nullptr;

            // Check for range (..)
            if (match(TokenKind::DotDot))
            {
                dim.low = std::move(expr);
                dim.high = parseExpression();
                if (!dim.high)
                    return nullptr;
            }
            else
            {
                // Single size means 0..(size-1), store just high for now
                // Semantic analysis will handle the interpretation
                dim.high = std::move(expr);
            }

            dimensions.push_back(std::move(dim));
        } while (match(TokenKind::Comma));

        if (!expect(TokenKind::RBracket, "']'"))
            return nullptr;
    }

    // Expect "of"
    if (!expect(TokenKind::KwOf, "'of'"))
        return nullptr;

    // Parse element type
    auto elemType = parseType();
    if (!elemType)
        return nullptr;

    return std::make_unique<ArrayTypeNode>(std::move(dimensions), std::move(elemType), loc);
}

std::unique_ptr<TypeNode> Parser::parseRecordType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwRecord, "'record'"))
        return nullptr;

    std::vector<RecordField> fields;

    // Parse fields until "end"
    while (!check(TokenKind::KwEnd) && !check(TokenKind::Eof))
    {
        auto fieldLoc = current_.loc;

        // Parse field names
        auto names = parseIdentList();
        if (names.empty())
        {
            resyncAfterError();
            continue;
        }

        // Expect ":"
        if (!expect(TokenKind::Colon, "':'"))
        {
            resyncAfterError();
            continue;
        }

        // Parse field type
        auto fieldType = parseType();
        if (!fieldType)
        {
            resyncAfterError();
            continue;
        }

        // Create a field for each name
        for (size_t i = 0; i < names.size(); ++i)
        {
            RecordField field;
            field.name = std::move(names[i]);
            if (i + 1 < names.size())
            {
                // Clone type for all but last name
                // For simplicity, we re-parse - but this is a limitation
                // In practice, all fields share the same type semantically
                field.type = std::make_unique<NamedTypeNode>(
                    static_cast<NamedTypeNode *>(fieldType.get())->name, fieldLoc);
            }
            else
            {
                field.type = std::move(fieldType);
            }
            field.loc = fieldLoc;
            fields.push_back(std::move(field));
        }

        // Semicolon after field declaration
        match(TokenKind::Semicolon);
    }

    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    return std::make_unique<RecordTypeNode>(std::move(fields), loc);
}

std::unique_ptr<TypeNode> Parser::parseEnumType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::LParen, "'('"))
        return nullptr;

    std::vector<std::string> values;

    // Parse enum values
    // Note: In Pascal, enum values can be any identifier-like token,
    // including keywords like 'div', 'mod', etc.
    if (!check(TokenKind::RParen))
    {
        do
        {
            // Accept identifiers and keyword tokens as enum values
            if (check(TokenKind::Identifier) || isKeyword(current_.kind))
            {
                values.push_back(current_.text);
                advance();
            }
            else
            {
                error("expected enum value identifier");
                return nullptr;
            }
        } while (match(TokenKind::Comma));
    }

    if (!expect(TokenKind::RParen, "')'"))
        return nullptr;

    return std::make_unique<EnumTypeNode>(std::move(values), loc);
}

std::unique_ptr<TypeNode> Parser::parsePointerType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::Caret, "'^'"))
        return nullptr;

    auto pointeeType = parseType();
    if (!pointeeType)
        return nullptr;

    return std::make_unique<PointerTypeNode>(std::move(pointeeType), loc);
}

std::unique_ptr<TypeNode> Parser::parseSetType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwSet, "'set'"))
        return nullptr;

    if (!expect(TokenKind::KwOf, "'of'"))
        return nullptr;

    auto elemType = parseType();
    if (!elemType)
        return nullptr;

    return std::make_unique<SetTypeNode>(std::move(elemType), loc);
}

std::unique_ptr<TypeNode> Parser::parseProcedureType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwProcedure, "'procedure'"))
        return nullptr;

    std::vector<ParamSpec> params;

    // Optional parameter list
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            auto paramDecls = parseParameters();
            for (auto &pd : paramDecls)
            {
                ParamSpec ps;
                ps.name = std::move(pd.name);
                ps.type = std::move(pd.type);
                ps.isVar = pd.isVar;
                ps.isConst = pd.isConst;
                ps.loc = pd.loc;
                params.push_back(std::move(ps));
            }
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    return std::make_unique<ProcedureTypeNode>(std::move(params), loc);
}

std::unique_ptr<TypeNode> Parser::parseFunctionType()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwFunction, "'function'"))
        return nullptr;

    std::vector<ParamSpec> params;

    // Optional parameter list
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            auto paramDecls = parseParameters();
            for (auto &pd : paramDecls)
            {
                ParamSpec ps;
                ps.name = std::move(pd.name);
                ps.type = std::move(pd.type);
                ps.isVar = pd.isVar;
                ps.isConst = pd.isConst;
                ps.loc = pd.loc;
                params.push_back(std::move(ps));
            }
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Expect return type
    if (!expect(TokenKind::Colon, "':'"))
        return nullptr;

    auto returnType = parseType();
    if (!returnType)
        return nullptr;

    return std::make_unique<FunctionTypeNode>(std::move(params), std::move(returnType), loc);
}

//=============================================================================
// Declaration Parsing
//=============================================================================

std::vector<std::unique_ptr<Decl>> Parser::parseDeclarations()
{
    std::vector<std::unique_ptr<Decl>> decls;

    while (true)
    {
        if (check(TokenKind::KwConst))
        {
            auto constDecls = parseConstSection();
            for (auto &d : constDecls)
                decls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwType))
        {
            auto typeDecls = parseTypeSection();
            for (auto &d : typeDecls)
                decls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwVar))
        {
            auto varDecls = parseVarSection();
            for (auto &d : varDecls)
                decls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwProcedure))
        {
            auto proc = parseProcedure();
            if (proc)
                decls.push_back(std::move(proc));
        }
        else if (check(TokenKind::KwFunction))
        {
            auto func = parseFunction();
            if (func)
                decls.push_back(std::move(func));
        }
        else if (check(TokenKind::KwConstructor))
        {
            auto ctor = parseConstructor();
            if (ctor)
                decls.push_back(std::move(ctor));
        }
        else if (check(TokenKind::KwDestructor))
        {
            auto dtor = parseDestructor();
            if (dtor)
                decls.push_back(std::move(dtor));
        }
        else
        {
            break;
        }
    }

    return decls;
}

std::vector<std::unique_ptr<Decl>> Parser::parseConstSection()
{
    std::vector<std::unique_ptr<Decl>> decls;

    if (!expect(TokenKind::KwConst, "'const'"))
        return decls;

    // Parse const declarations until we hit another section keyword or begin
    while (check(TokenKind::Identifier))
    {
        auto loc = current_.loc;
        auto name = current_.text;
        advance();

        // Optional type annotation
        std::unique_ptr<TypeNode> type;
        if (match(TokenKind::Colon))
        {
            type = parseType();
        }

        // Expect "="
        if (!expect(TokenKind::Equal, "'='"))
        {
            resyncAfterError();
            continue;
        }

        // Parse value expression
        auto value = parseExpression();
        if (!value)
        {
            resyncAfterError();
            continue;
        }

        // Expect ";"
        if (!expect(TokenKind::Semicolon, "';'"))
        {
            resyncAfterError();
        }

        decls.push_back(std::make_unique<ConstDecl>(
            std::move(name), std::move(value), std::move(type), loc));
    }

    return decls;
}

std::vector<std::unique_ptr<Decl>> Parser::parseTypeSection()
{
    std::vector<std::unique_ptr<Decl>> decls;

    if (!expect(TokenKind::KwType, "'type'"))
        return decls;

    // Parse type declarations until we hit another section keyword or begin
    while (check(TokenKind::Identifier))
    {
        auto loc = current_.loc;
        auto name = current_.text;
        advance();

        // Expect "="
        if (!expect(TokenKind::Equal, "'='"))
        {
            resyncAfterError();
            continue;
        }

        // Check for class or interface
        if (check(TokenKind::KwClass))
        {
            advance();
            auto classDecl = parseClass(name, loc);
            if (classDecl)
                decls.push_back(std::move(classDecl));
            continue;
        }

        if (check(TokenKind::KwInterface))
        {
            advance();
            auto ifaceDecl = parseInterface(name, loc);
            if (ifaceDecl)
                decls.push_back(std::move(ifaceDecl));
            continue;
        }

        // Parse type definition
        auto type = parseType();
        if (!type)
        {
            resyncAfterError();
            continue;
        }

        // Expect ";"
        if (!expect(TokenKind::Semicolon, "';'"))
        {
            resyncAfterError();
        }

        decls.push_back(std::make_unique<TypeDecl>(std::move(name), std::move(type), loc));
    }

    return decls;
}

std::vector<std::unique_ptr<Decl>> Parser::parseVarSection()
{
    std::vector<std::unique_ptr<Decl>> decls;

    if (!expect(TokenKind::KwVar, "'var'"))
        return decls;

    // Parse var declarations until we hit another section keyword or begin
    while (check(TokenKind::Identifier))
    {
        auto loc = current_.loc;

        // Parse identifier list
        auto names = parseIdentList();
        if (names.empty())
        {
            resyncAfterError();
            continue;
        }

        // Expect ":"
        if (!expect(TokenKind::Colon, "':'"))
        {
            resyncAfterError();
            continue;
        }

        // Parse type
        auto type = parseType();
        if (!type)
        {
            resyncAfterError();
            continue;
        }

        // Optional initializer
        std::unique_ptr<Expr> init;
        if (match(TokenKind::Equal))
        {
            init = parseExpression();
            if (!init)
            {
                resyncAfterError();
                continue;
            }
        }

        // Expect ";"
        if (!expect(TokenKind::Semicolon, "';'"))
        {
            resyncAfterError();
        }

        decls.push_back(std::make_unique<VarDecl>(
            std::move(names), std::move(type), std::move(init), loc));
    }

    return decls;
}

std::unique_ptr<Decl> Parser::parseProcedure()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwProcedure, "'procedure'"))
        return nullptr;

    // Parse name - may be ClassName.MethodName for method implementations
    if (!check(TokenKind::Identifier))
    {
        error("expected procedure name");
        return nullptr;
    }
    auto name = current_.text;
    std::string className;
    advance();

    // Check for ClassName.MethodName format
    if (match(TokenKind::Dot))
    {
        // name is actually the class name
        className = name;
        if (!check(TokenKind::Identifier))
        {
            error("expected method name after '.'");
            return nullptr;
        }
        name = current_.text;
        advance();
    }

    // Parse parameters
    std::vector<ParamDecl> params;
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            params = parseParameters();
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    auto decl = std::make_unique<ProcedureDecl>(std::move(name), std::move(params), loc);
    decl->className = std::move(className);

    // Check for forward declaration
    if (check(TokenKind::KwForward))
    {
        advance();
        decl->isForward = true;
        expect(TokenKind::Semicolon, "';'");
        return decl;
    }

    // Parse local declarations
    decl->localDecls = parseDeclarations();

    // Parse body
    if (check(TokenKind::KwBegin))
    {
        decl->body = parseBlock();
        expect(TokenKind::Semicolon, "';'");
    }

    return decl;
}

std::unique_ptr<Decl> Parser::parseFunction()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwFunction, "'function'"))
        return nullptr;

    // Parse name - may be ClassName.MethodName for method implementations
    if (!check(TokenKind::Identifier))
    {
        error("expected function name");
        return nullptr;
    }
    auto name = current_.text;
    std::string className;
    advance();

    // Check for ClassName.MethodName format
    if (match(TokenKind::Dot))
    {
        // name is actually the class name
        className = name;
        if (!check(TokenKind::Identifier))
        {
            error("expected method name after '.'");
            return nullptr;
        }
        name = current_.text;
        advance();
    }

    // Parse parameters
    std::vector<ParamDecl> params;
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            params = parseParameters();
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Expect return type
    if (!expect(TokenKind::Colon, "':'"))
        return nullptr;

    auto returnType = parseType();
    if (!returnType)
        return nullptr;

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    auto decl = std::make_unique<FunctionDecl>(
        std::move(name), std::move(params), std::move(returnType), loc);
    decl->className = std::move(className);

    // Check for forward declaration
    if (check(TokenKind::KwForward))
    {
        advance();
        decl->isForward = true;
        expect(TokenKind::Semicolon, "';'");
        return decl;
    }

    // Parse local declarations
    decl->localDecls = parseDeclarations();

    // Parse body
    if (check(TokenKind::KwBegin))
    {
        decl->body = parseBlock();
        expect(TokenKind::Semicolon, "';'");
    }

    return decl;
}

std::vector<ParamDecl> Parser::parseParameters()
{
    std::vector<ParamDecl> params;

    // Parse first parameter group
    auto group = parseParamGroup();
    for (auto &p : group)
        params.push_back(std::move(p));

    // Parse remaining parameter groups
    while (match(TokenKind::Semicolon))
    {
        group = parseParamGroup();
        for (auto &p : group)
            params.push_back(std::move(p));
    }

    return params;
}

std::vector<ParamDecl> Parser::parseParamGroup()
{
    std::vector<ParamDecl> params;
    auto loc = current_.loc;

    // Check for var/const modifier
    bool isVar = false;
    bool isConst = false;
    if (match(TokenKind::KwVar))
    {
        isVar = true;
    }
    else if (match(TokenKind::KwConst))
    {
        isConst = true;
    }

    // Parse identifier list
    auto names = parseIdentList();
    if (names.empty())
        return params;

    // Expect ":"
    if (!expect(TokenKind::Colon, "':'"))
        return params;

    // Parse type
    auto type = parseType();
    if (!type)
        return params;

    // Optional default value
    std::unique_ptr<Expr> defaultValue;
    if (match(TokenKind::Equal))
    {
        defaultValue = parseExpression();
    }

    // Create a ParamDecl for each name
    for (size_t i = 0; i < names.size(); ++i)
    {
        ParamDecl pd;
        pd.name = std::move(names[i]);
        pd.isVar = isVar;
        pd.isConst = isConst;
        pd.loc = loc;

        // Clone type for all but last
        if (i + 1 < names.size())
        {
            // Simple clone for named types
            if (type->kind == TypeKind::Named)
            {
                pd.type = std::make_unique<NamedTypeNode>(
                    static_cast<NamedTypeNode *>(type.get())->name, type->loc);
            }
            else
            {
                // For complex types, just reference the same type
                // Semantic analysis will handle this
                pd.type = std::make_unique<NamedTypeNode>("?", type->loc);
            }
        }
        else
        {
            pd.type = std::move(type);
            if (defaultValue)
                pd.defaultValue = std::move(defaultValue);
        }

        params.push_back(std::move(pd));
    }

    return params;
}

std::unique_ptr<Decl> Parser::parseClass(const std::string &name, il::support::SourceLoc loc)
{
    auto decl = std::make_unique<ClassDecl>(name, loc);

    // Optional heritage clause: (BaseClass, Interface1, Interface2)
    if (match(TokenKind::LParen))
    {
        // First identifier is base class
        if (check(TokenKind::Identifier))
        {
            decl->baseClass = current_.text;
            advance();

            // Additional identifiers are interfaces
            while (match(TokenKind::Comma))
            {
                if (!check(TokenKind::Identifier))
                {
                    error("expected interface name");
                    break;
                }
                decl->interfaces.push_back(current_.text);
                advance();
            }
        }

        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Parse class body
    Visibility currentVisibility = Visibility::Public;

    while (!check(TokenKind::KwEnd) && !check(TokenKind::Eof))
    {
        // Skip stray semicolons (can happen after error recovery)
        if (check(TokenKind::Semicolon))
        {
            advance();
            continue;
        }

        // Check for visibility specifier
        if (check(TokenKind::KwPrivate))
        {
            advance();
            currentVisibility = Visibility::Private;
            continue;
        }
        if (check(TokenKind::KwPublic))
        {
            advance();
            currentVisibility = Visibility::Public;
            continue;
        }

        // Parse member
        auto member = parseClassMember(currentVisibility);
        decl->members.push_back(std::move(member));
    }

    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    expect(TokenKind::Semicolon, "';'");

    return decl;
}

std::unique_ptr<Decl> Parser::parseInterface(const std::string &name, il::support::SourceLoc loc)
{
    auto decl = std::make_unique<InterfaceDecl>(name, loc);

    // Optional heritage clause: (Interface1, Interface2)
    if (match(TokenKind::LParen))
    {
        do
        {
            if (!check(TokenKind::Identifier))
            {
                error("expected interface name");
                break;
            }
            decl->baseInterfaces.push_back(current_.text);
            advance();
        } while (match(TokenKind::Comma));

        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Parse interface methods
    while (!check(TokenKind::KwEnd) && !check(TokenKind::Eof))
    {
        auto methodLoc = current_.loc;
        MethodSig sig;
        sig.loc = methodLoc;

        if (check(TokenKind::KwProcedure))
        {
            advance();

            if (!check(TokenKind::Identifier))
            {
                error("expected method name");
                resyncAfterError();
                continue;
            }
            sig.name = current_.text;
            advance();

            // Parse parameters
            if (match(TokenKind::LParen))
            {
                if (!check(TokenKind::RParen))
                {
                    auto params = parseParameters();
                    for (auto &pd : params)
                    {
                        ParamDecl p;
                        p.name = std::move(pd.name);
                        p.type = std::move(pd.type);
                        p.isVar = pd.isVar;
                        p.isConst = pd.isConst;
                        p.loc = pd.loc;
                        sig.params.push_back(std::move(p));
                    }
                }
                expect(TokenKind::RParen, "')'");
            }

            expect(TokenKind::Semicolon, "';'");
        }
        else if (check(TokenKind::KwFunction))
        {
            advance();

            if (!check(TokenKind::Identifier))
            {
                error("expected method name");
                resyncAfterError();
                continue;
            }
            sig.name = current_.text;
            advance();

            // Parse parameters
            if (match(TokenKind::LParen))
            {
                if (!check(TokenKind::RParen))
                {
                    auto params = parseParameters();
                    for (auto &pd : params)
                    {
                        ParamDecl p;
                        p.name = std::move(pd.name);
                        p.type = std::move(pd.type);
                        p.isVar = pd.isVar;
                        p.isConst = pd.isConst;
                        p.loc = pd.loc;
                        sig.params.push_back(std::move(p));
                    }
                }
                expect(TokenKind::RParen, "')'");
            }

            // Return type
            if (!expect(TokenKind::Colon, "':'"))
            {
                resyncAfterError();
                continue;
            }
            sig.returnType = parseType();

            expect(TokenKind::Semicolon, "';'");
        }
        else
        {
            error("expected 'procedure' or 'function' in interface");
            resyncAfterError();
            continue;
        }

        decl->methods.push_back(std::move(sig));
    }

    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    expect(TokenKind::Semicolon, "';'");

    return decl;
}

ClassMember Parser::parseClassMember(Visibility currentVisibility)
{
    ClassMember member;
    member.visibility = currentVisibility;
    member.loc = current_.loc;

    // Constructor (signature only in class declaration)
    if (check(TokenKind::KwConstructor))
    {
        member.memberKind = ClassMember::Kind::Constructor;
        member.methodDecl = parseConstructorSignature();
        return member;
    }

    // Destructor (signature only in class declaration)
    if (check(TokenKind::KwDestructor))
    {
        member.memberKind = ClassMember::Kind::Destructor;
        member.methodDecl = parseDestructorSignature();
        return member;
    }

    // Procedure method (signature only in class)
    if (check(TokenKind::KwProcedure))
    {
        member.memberKind = ClassMember::Kind::Method;
        member.methodDecl = parseMethodSignature(/*isFunction=*/false);
        return member;
    }

    // Function method (signature only in class)
    if (check(TokenKind::KwFunction))
    {
        member.memberKind = ClassMember::Kind::Method;
        member.methodDecl = parseMethodSignature(/*isFunction=*/true);
        return member;
    }

    // Field: [weak] ident_list : type ;
    member.memberKind = ClassMember::Kind::Field;

    // Check for weak modifier
    if (check(TokenKind::KwWeak))
    {
        member.isWeak = true;
        advance();
    }

    // Parse first field name
    if (!check(TokenKind::Identifier))
    {
        error("expected field name");
        resyncAfterError();
        return member;
    }
    member.fieldName = current_.text;
    advance();

    // Handle comma-separated field names: x, y, z: Type;
    // For now, we only support single field declarations in class members
    // If we see a comma, skip to colon (the fields share a type)
    while (match(TokenKind::Comma))
    {
        // Skip additional field names - they share the same type
        // A more complete implementation would create separate members
        if (check(TokenKind::Identifier))
        {
            advance();
        }
    }

    // Expect ":"
    if (!expect(TokenKind::Colon, "':'"))
    {
        resyncAfterError();
        return member;
    }

    // Parse type
    member.fieldType = parseType();

    // Expect ";"
    expect(TokenKind::Semicolon, "';'");

    return member;
}

std::unique_ptr<Decl> Parser::parseMethodSignature(bool isFunction)
{
    auto loc = current_.loc;

    // Consume procedure/function keyword
    if (isFunction)
    {
        if (!expect(TokenKind::KwFunction, "'function'"))
            return nullptr;
    }
    else
    {
        if (!expect(TokenKind::KwProcedure, "'procedure'"))
            return nullptr;
    }

    // Parse name
    if (!check(TokenKind::Identifier))
    {
        error(isFunction ? "expected function name" : "expected procedure name");
        return nullptr;
    }
    auto name = current_.text;
    advance();

    // Parse parameters
    std::vector<ParamDecl> params;
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            params = parseParameters();
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // For functions, parse return type
    std::unique_ptr<TypeNode> returnType;
    if (isFunction)
    {
        if (!expect(TokenKind::Colon, "':'"))
            return nullptr;
        returnType = parseType();
        if (!returnType)
            return nullptr;
    }

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    // Handle optional method modifiers (virtual, override)
    bool isVirtual = false;
    bool isOverride = false;
    while (check(TokenKind::KwVirtual) || check(TokenKind::KwOverride))
    {
        if (match(TokenKind::KwVirtual))
            isVirtual = true;
        if (match(TokenKind::KwOverride))
            isOverride = true;
        // Expect ";" after modifier
        if (!expect(TokenKind::Semicolon, "';'"))
            return nullptr;
    }

    // Create the appropriate decl (signature only, no body)
    if (isFunction)
    {
        auto decl = std::make_unique<FunctionDecl>(std::move(name), std::move(params),
                                                   std::move(returnType), loc);
        decl->isForward = true; // Treat as forward declaration (no body)
        decl->isVirtual = isVirtual;
        decl->isOverride = isOverride;
        return decl;
    }
    else
    {
        auto decl = std::make_unique<ProcedureDecl>(std::move(name), std::move(params), loc);
        decl->isForward = true; // Treat as forward declaration (no body)
        decl->isVirtual = isVirtual;
        decl->isOverride = isOverride;
        return decl;
    }
}

std::unique_ptr<Decl> Parser::parseConstructor()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwConstructor, "'constructor'"))
        return nullptr;

    // Parse name - may be ClassName.MethodName for method implementations
    if (!check(TokenKind::Identifier))
    {
        error("expected constructor name");
        return nullptr;
    }
    auto name = current_.text;
    std::string className;
    advance();

    // Check for ClassName.MethodName format
    if (match(TokenKind::Dot))
    {
        // name is actually the class name
        className = name;
        if (!check(TokenKind::Identifier))
        {
            error("expected constructor name after '.'");
            return nullptr;
        }
        name = current_.text;
        advance();
    }

    // Parse parameters
    std::vector<ParamDecl> params;
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            params = parseParameters();
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    auto decl = std::make_unique<ConstructorDecl>(std::move(name), std::move(params), loc);
    decl->className = std::move(className);

    // Parse local declarations
    decl->localDecls = parseDeclarations();

    // Parse body
    if (check(TokenKind::KwBegin))
    {
        decl->body = parseBlock();
        expect(TokenKind::Semicolon, "';'");
    }

    return decl;
}

std::unique_ptr<Decl> Parser::parseDestructor()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwDestructor, "'destructor'"))
        return nullptr;

    // Parse name - may be ClassName.MethodName for method implementations
    if (!check(TokenKind::Identifier))
    {
        error("expected destructor name");
        return nullptr;
    }
    auto name = current_.text;
    std::string className;
    advance();

    // Check for ClassName.MethodName format
    if (match(TokenKind::Dot))
    {
        // name is actually the class name
        className = name;
        if (!check(TokenKind::Identifier))
        {
            error("expected destructor name after '.'");
            return nullptr;
        }
        name = current_.text;
        advance();
    }

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    auto decl = std::make_unique<DestructorDecl>(std::move(name), loc);
    decl->className = std::move(className);

    // Parse local declarations
    decl->localDecls = parseDeclarations();

    // Parse body
    if (check(TokenKind::KwBegin))
    {
        decl->body = parseBlock();
        expect(TokenKind::Semicolon, "';'");
    }

    return decl;
}

std::unique_ptr<Decl> Parser::parseConstructorSignature()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwConstructor, "'constructor'"))
        return nullptr;

    // Parse name
    if (!check(TokenKind::Identifier))
    {
        error("expected constructor name");
        return nullptr;
    }
    auto name = current_.text;
    advance();

    // Parse parameters
    std::vector<ParamDecl> params;
    if (match(TokenKind::LParen))
    {
        if (!check(TokenKind::RParen))
        {
            params = parseParameters();
        }
        if (!expect(TokenKind::RParen, "')'"))
            return nullptr;
    }

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    auto decl = std::make_unique<ConstructorDecl>(std::move(name), std::move(params), loc);
    decl->isForward = true;  // Mark as signature only
    return decl;
}

std::unique_ptr<Decl> Parser::parseDestructorSignature()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwDestructor, "'destructor'"))
        return nullptr;

    // Parse name
    if (!check(TokenKind::Identifier))
    {
        error("expected destructor name");
        return nullptr;
    }
    auto name = current_.text;
    advance();

    // Expect ";"
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    auto decl = std::make_unique<DestructorDecl>(std::move(name), loc);
    decl->isForward = true;  // Mark as signature only
    return decl;
}

std::vector<std::string> Parser::parseIdentList()
{
    std::vector<std::string> names;

    if (!check(TokenKind::Identifier))
    {
        error("expected identifier");
        return names;
    }

    names.push_back(current_.text);
    advance();

    while (match(TokenKind::Comma))
    {
        if (!check(TokenKind::Identifier))
        {
            error("expected identifier after ','");
            break;
        }
        names.push_back(current_.text);
        advance();
    }

    return names;
}

//=============================================================================
// Program/Unit Parsing
//=============================================================================

std::pair<std::unique_ptr<Program>, std::unique_ptr<Unit>> Parser::parse()
{
    if (check(TokenKind::KwProgram))
    {
        return {parseProgram(), nullptr};
    }
    else if (check(TokenKind::KwUnit))
    {
        return {nullptr, parseUnit()};
    }
    else
    {
        error("expected 'program' or 'unit'");
        return {nullptr, nullptr};
    }
}

std::unique_ptr<Program> Parser::parseProgram()
{
    auto program = std::make_unique<Program>();
    program->loc = current_.loc;

    // Expect "program"
    if (!expect(TokenKind::KwProgram, "'program'"))
        return nullptr;

    // Expect program name
    if (!check(TokenKind::Identifier))
    {
        error("expected program name");
        return nullptr;
    }
    program->name = current_.text;
    advance();

    // Expect semicolon after program name
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    // Optional uses clause
    if (check(TokenKind::KwUses))
    {
        program->usedUnits = parseUses();
    }

    // Parse declarations
    program->decls = parseDeclarations();

    // Parse main block
    program->body = parseBlock();
    if (!program->body)
        return nullptr;

    // Expect final dot
    if (!expect(TokenKind::Dot, "'.'"))
        return nullptr;

    return program;
}

std::unique_ptr<Unit> Parser::parseUnit()
{
    auto unit = std::make_unique<Unit>();
    unit->loc = current_.loc;

    // Expect "unit"
    if (!expect(TokenKind::KwUnit, "'unit'"))
        return nullptr;

    // Expect unit name
    if (!check(TokenKind::Identifier))
    {
        error("expected unit name");
        return nullptr;
    }
    unit->name = current_.text;
    advance();

    // Expect semicolon
    if (!expect(TokenKind::Semicolon, "';'"))
        return nullptr;

    // Expect "interface"
    if (!expect(TokenKind::KwInterface, "'interface'"))
        return nullptr;

    // Optional uses clause in interface section
    if (check(TokenKind::KwUses))
    {
        unit->usedUnits = parseUses();
    }

    // Parse interface declarations (const, type, var, proc/func signatures)
    while (!check(TokenKind::KwImplementation) && !check(TokenKind::Eof))
    {
        if (check(TokenKind::KwConst))
        {
            auto decls = parseConstSection();
            for (auto &d : decls)
                unit->interfaceDecls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwType))
        {
            auto decls = parseTypeSection();
            for (auto &d : decls)
                unit->interfaceDecls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwVar))
        {
            auto decls = parseVarSection();
            for (auto &d : decls)
                unit->interfaceDecls.push_back(std::move(d));
        }
        else if (check(TokenKind::KwProcedure))
        {
            // Parse just the signature (forward declaration)
            auto proc = parseProcedure();
            if (proc)
            {
                static_cast<ProcedureDecl *>(proc.get())->isForward = true;
                unit->interfaceDecls.push_back(std::move(proc));
            }
        }
        else if (check(TokenKind::KwFunction))
        {
            // Parse just the signature (forward declaration)
            auto func = parseFunction();
            if (func)
            {
                static_cast<FunctionDecl *>(func.get())->isForward = true;
                unit->interfaceDecls.push_back(std::move(func));
            }
        }
        else
        {
            break;
        }
    }

    // Expect "implementation"
    if (!expect(TokenKind::KwImplementation, "'implementation'"))
        return nullptr;

    // Optional uses clause in implementation section
    if (check(TokenKind::KwUses))
    {
        unit->implUsedUnits = parseUses();
    }

    // Parse implementation declarations
    unit->implDecls = parseDeclarations();

    // Optional initialization section
    if (check(TokenKind::KwInitialization))
    {
        advance();
        auto stmts = parseStatementList();
        unit->initSection = std::make_unique<BlockStmt>(std::move(stmts), current_.loc);
    }

    // Optional finalization section
    if (check(TokenKind::KwFinalization))
    {
        advance();
        auto stmts = parseStatementList();
        unit->finalSection = std::make_unique<BlockStmt>(std::move(stmts), current_.loc);
    }

    // Expect "end."
    if (!expect(TokenKind::KwEnd, "'end'"))
        return nullptr;

    if (!expect(TokenKind::Dot, "'.'"))
        return nullptr;

    return unit;
}

std::vector<std::string> Parser::parseUses()
{
    std::vector<std::string> units;

    if (!expect(TokenKind::KwUses, "'uses'"))
        return units;

    // First unit name
    if (!check(TokenKind::Identifier))
    {
        error("expected unit name");
        return units;
    }
    units.push_back(current_.text);
    advance();

    // Additional unit names
    while (match(TokenKind::Comma))
    {
        if (!check(TokenKind::Identifier))
        {
            error("expected unit name after ','");
            break;
        }
        units.push_back(current_.text);
        advance();
    }

    // Expect semicolon
    expect(TokenKind::Semicolon, "';'");

    return units;
}

} // namespace il::frontends::pascal
