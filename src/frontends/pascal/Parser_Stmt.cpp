//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Parser_Stmt.cpp
// Purpose: Statement parsing for Viper Pascal.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Parser.hpp"

namespace il::frontends::pascal
{


std::unique_ptr<Stmt> Parser::parseStatement()
{
    auto loc = current_.loc;

    // Empty statement (just semicolon or end of block)
    if (check(TokenKind::Semicolon) || check(TokenKind::KwEnd) || check(TokenKind::KwElse) ||
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

    // Exit statement: Exit; or Exit(value);
    if (check(TokenKind::KwExit))
    {
        advance();
        std::unique_ptr<Expr> value;
        // Check for optional value: Exit(value)
        if (match(TokenKind::LParen))
        {
            value = parseExpression();
            if (!expect(TokenKind::RParen, "')'"))
                return nullptr;
        }
        return std::make_unique<ExitStmt>(std::move(value), loc);
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

    // With statement
    if (check(TokenKind::KwWith))
    {
        return parseWith();
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

std::unique_ptr<Stmt> Parser::parseWith()
{
    auto loc = current_.loc;

    if (!expect(TokenKind::KwWith, "'with'"))
        return nullptr;

    // Parse one or more expressions separated by commas
    // with expr1, expr2, ... do statement
    std::vector<std::unique_ptr<Expr>> objects;
    do
    {
        auto expr = parseExpression();
        if (!expr)
            return nullptr;
        objects.push_back(std::move(expr));
    } while (match(TokenKind::Comma));

    if (!expect(TokenKind::KwDo, "'do'"))
        return nullptr;

    auto body = parseStatement();
    if (!body)
        return nullptr;

    return std::make_unique<WithStmt>(std::move(objects), std::move(body), loc);
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

        return std::make_unique<ForStmt>(std::move(loopVar),
                                         std::move(start),
                                         std::move(bound),
                                         direction,
                                         std::move(body),
                                         loc);
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

        // Check for unsupported range syntax (a..b)
        if (check(TokenKind::DotDot))
        {
            error("case ranges (a..b) are not supported in Viper Pascal v0.1; "
                  "list individual values instead");
            return nullptr;
        }

        while (match(TokenKind::Comma))
        {
            label = parseExpression();
            if (!label)
                return nullptr;
            arm.labels.push_back(std::move(label));

            // Check for unsupported range syntax after comma
            if (check(TokenKind::DotDot))
            {
                error("case ranges (a..b) are not supported in Viper Pascal v0.1; "
                      "list individual values instead");
                return nullptr;
            }
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
        // Don't parse another statement if we're at a section terminator
        if (check(TokenKind::KwEnd) || check(TokenKind::KwUntil) || check(TokenKind::KwElse) ||
            check(TokenKind::KwFinalization) || check(TokenKind::KwInitialization))
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
    if (!check(TokenKind::Semicolon) && !check(TokenKind::KwEnd) && !check(TokenKind::KwElse))
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
    while (!check(TokenKind::KwExcept) && !check(TokenKind::KwFinally) &&
           !check(TokenKind::KwEnd) && !check(TokenKind::Eof))
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


} // namespace il::frontends::pascal
