//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Parser_Stmt.cpp
/// @brief Statement parsing implementation for the Zia parser.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Parser.hpp"

namespace il::frontends::zia
{

//===----------------------------------------------------------------------===//
// Statement Parsing
//===----------------------------------------------------------------------===//

/// @brief Parse a statement, dispatching to the appropriate parser based on the leading token.
/// @details Handles blocks, var/final declarations, Java-style declarations, if, while, for,
///          return, guard, match, break, continue, print/println, and expression statements.
/// @return The parsed statement, or nullptr on error.
StmtPtr Parser::parseStatement()
{
    SourceLoc loc = peek().loc;

    // Block
    if (check(TokenKind::LBrace))
    {
        return parseBlock();
    }

    // var/final variable declaration: var x = 5; final y: Integer = 10;
    if (check(TokenKind::KwVar) || check(TokenKind::KwFinal))
    {
        return parseVarDecl();
    }

    // Java-style variable declaration: Type name = expr;
    // Try parsing it speculatively (no heuristics); if it fails, fall back to expression parsing.
    if (check(TokenKind::Identifier) || check(TokenKind::LParen))
    {
        Speculation speculative(*this);
        if (StmtPtr decl = parseJavaStyleVarDecl())
        {
            speculative.commit();
            return decl;
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

    // Match statement (only when followed by a scrutinee, not when used as identifier)
    if (check(TokenKind::KwMatch))
    {
        auto nextKind = peek(1).kind;
        bool isMatchStmt = (nextKind == TokenKind::Identifier ||
                            nextKind == TokenKind::IntegerLiteral ||
                            nextKind == TokenKind::NumberLiteral ||
                            nextKind == TokenKind::StringLiteral ||
                            nextKind == TokenKind::LParen ||
                            nextKind == TokenKind::KwTrue ||
                            nextKind == TokenKind::KwFalse ||
                            nextKind == TokenKind::KwNull ||
                            nextKind == TokenKind::KwSelf);
        if (isMatchStmt)
        {
            return parseMatchStmt();
        }
        // else: fall through to expression statement parsing (e.g., match = 10;)
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

/// @brief Parse a braced statement block ({ stmt; stmt; ... }).
/// @details Includes error recovery: on parse failure, skips to the next semicolon or brace
///          to continue parsing subsequent statements.
/// @return The parsed BlockStmt, or nullptr on error.
StmtPtr Parser::parseBlock()
{
    Token lbraceTok;
    if (!expect(TokenKind::LBrace, "{", &lbraceTok))
        return nullptr;
    SourceLoc loc = lbraceTok.loc;

    std::vector<StmtPtr> statements;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        // Check for declaration keywords that shouldn't appear inside a block.
        // If we see these, the block was likely not properly closed.
        // Note: 'value' is not included because it can be used as an identifier
        // (e.g., "Integer value = 0;"). We only check for unambiguous declaration starters.
        // 'func' is always a declaration keyword and cannot be used as an identifier.
        if (check(TokenKind::KwFunc) ||
            (check(TokenKind::KwExpose) && check(TokenKind::KwFunc, 1)) ||
            (check(TokenKind::KwHide) && check(TokenKind::KwFunc, 1)) ||
            check(TokenKind::KwEntity) || check(TokenKind::KwInterface))
        {
            error("unexpected declaration keyword in block - possible missing '}'");
            break;
        }

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

/// @brief Parse a local variable declaration (var x: Type = expr; or final x = expr;).
/// @return The parsed VarStmt, or nullptr on error.
StmtPtr Parser::parseVarDecl()
{
    Token kwTok = advance(); // consume var/final
    SourceLoc loc = kwTok.loc;
    bool isFinal = kwTok.kind == TokenKind::KwFinal;

    if (!checkIdentifierLike())
    {
        error("expected variable name");
        return nullptr;
    }
    Token nameTok = advance();
    std::string name = nameTok.text;

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

/// @brief Parse a Java-style local variable declaration (Type name = expr;).
/// @details Used speculatively; returns nullptr if the token sequence does not match.
/// @return The parsed VarStmt, or nullptr if not a valid Java-style declaration.
StmtPtr Parser::parseJavaStyleVarDecl()
{
    SourceLoc loc = peek().loc;

    // Parse the type (e.g., Integer, List[String], etc.)
    TypePtr type = parseType();
    if (!type)
        return nullptr;

    // Now we expect a variable name
    if (!checkIdentifierLike())
    {
        error("expected variable name after type");
        return nullptr;
    }
    Token nameTok = advance();
    std::string name = nameTok.text;

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
    return std::make_unique<VarStmt>(loc, std::move(name), std::move(type), std::move(init), false);
}

/// @brief Parse an if statement with optional else clause (if cond { body } else { body }).
/// @return The parsed IfStmt, or nullptr on error.
StmtPtr Parser::parseIfStmt()
{
    Token ifTok = advance(); // consume 'if'
    SourceLoc loc = ifTok.loc;

    // Zia uses "if condition {" without parentheses
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

/// @brief Parse a while loop (while condition { body }).
/// @return The parsed WhileStmt, or nullptr on error.
StmtPtr Parser::parseWhileStmt()
{
    Token whileTok = advance(); // consume 'while'
    SourceLoc loc = whileTok.loc;

    // Zia uses "while condition {" without parentheses
    ExprPtr condition = parseExpression();
    if (!condition)
        return nullptr;

    StmtPtr body = parseStatement();
    if (!body)
        return nullptr;

    return std::make_unique<WhileStmt>(loc, std::move(condition), std::move(body));
}

/// @brief Parse a for statement, supporting both C-style and for-in forms.
/// @details C-style: for (init; cond; update) { body }
///          For-in: for x in collection { body }
///          For-in tuple: for (k, v) in map { body }
/// @return The parsed ForStmt or ForInStmt, or nullptr on error.
StmtPtr Parser::parseForStmt()
{
    Token forTok = advance(); // consume 'for'
    SourceLoc loc = forTok.loc;

    bool hasParen = match(TokenKind::LParen);

    auto isCStyleFor = [&]() -> bool
    {
        int depth = 0;
        for (int i = 0;; ++i)
        {
            TokenKind kind = peek(i).kind;
            if (kind == TokenKind::Eof)
                break;
            if (!hasParen && kind == TokenKind::LBrace)
                break;
            if (kind == TokenKind::LParen)
            {
                depth++;
                continue;
            }
            if (kind == TokenKind::RParen)
            {
                if (depth == 0)
                    break;
                depth--;
                continue;
            }
            if (depth == 0)
            {
                if (kind == TokenKind::Semicolon)
                    return true;
                if (kind == TokenKind::KwIn)
                    return false;
            }
        }
        return false;
    };

    if (isCStyleFor())
    {
        if (!hasParen)
        {
            error("expected '(' in C-style for loop");
            return nullptr;
        }

        StmtPtr init;
        if (!check(TokenKind::Semicolon))
        {
            if (check(TokenKind::KwVar) || check(TokenKind::KwFinal))
            {
                init = parseVarDecl();
                if (!init)
                    return nullptr;
            }
            else
            {
                ExprPtr initExpr = parseExpression();
                if (!initExpr)
                    return nullptr;
                if (!expect(TokenKind::Semicolon, ";"))
                    return nullptr;
                init = std::make_unique<ExprStmt>(initExpr->loc, std::move(initExpr));
            }
        }
        else if (!expect(TokenKind::Semicolon, ";"))
        {
            return nullptr;
        }

        ExprPtr condition;
        if (!check(TokenKind::Semicolon))
        {
            condition = parseExpression();
            if (!condition)
                return nullptr;
        }
        if (!expect(TokenKind::Semicolon, ";"))
            return nullptr;

        ExprPtr update;
        if (!check(TokenKind::RParen))
        {
            update = parseExpression();
            if (!update)
                return nullptr;
        }

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        StmtPtr body = parseStatement();
        if (!body)
            return nullptr;

        return std::make_unique<ForStmt>(
            loc, std::move(init), std::move(condition), std::move(update), std::move(body));
    }

    // Optional extra parentheses for tuple binding: for ((a, b) in ...)
    bool hasTupleParen = false;
    if (hasParen && check(TokenKind::LParen))
    {
        hasTupleParen = true;
        advance();
    }

    if (!checkIdentifierLike())
    {
        error("expected variable name in for loop");
        return nullptr;
    }

    Token varTok = advance();
    std::string varName = varTok.text;

    TypePtr varType;
    if (match(TokenKind::Colon))
    {
        varType = parseType();
        if (!varType)
            return nullptr;
    }

    bool isTuple = false;
    std::string secondVar;
    TypePtr secondType;

    if (match(TokenKind::Comma))
    {
        isTuple = true;
        if (!checkIdentifierLike())
        {
            error("expected variable name in tuple binding");
            return nullptr;
        }
        Token secondTok = advance();
        secondVar = secondTok.text;

        if (match(TokenKind::Colon))
        {
            secondType = parseType();
            if (!secondType)
                return nullptr;
        }
    }

    if (hasTupleParen)
    {
        if (!expect(TokenKind::RParen, ")"))
            return nullptr;
    }

    if (!expect(TokenKind::KwIn, "in"))
        return nullptr;

    ExprPtr iterable = parseExpression();
    if (!iterable)
        return nullptr;

    if (hasParen)
    {
        if (!expect(TokenKind::RParen, ")"))
            return nullptr;
    }

    StmtPtr body = parseStatement();
    if (!body)
        return nullptr;

    std::unique_ptr<ForInStmt> stmt;
    if (isTuple)
    {
        stmt = std::make_unique<ForInStmt>(
            loc, std::move(varName), std::move(secondVar), std::move(iterable), std::move(body));
        stmt->secondVariableType = std::move(secondType);
    }
    else
    {
        stmt = std::make_unique<ForInStmt>(
            loc, std::move(varName), std::move(iterable), std::move(body));
    }
    stmt->variableType = std::move(varType);
    return stmt;
}

/// @brief Parse a return statement (return [expr];).
/// @return The parsed ReturnStmt, or nullptr on error.
StmtPtr Parser::parseReturnStmt()
{
    Token returnTok = advance(); // consume 'return'
    SourceLoc loc = returnTok.loc;

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

/// @brief Parse a guard statement (guard condition else { body }).
/// @details The else block must contain a control flow exit (return, break, continue).
/// @return The parsed GuardStmt, or nullptr on error.
StmtPtr Parser::parseGuardStmt()
{
    Token guardTok = advance(); // consume 'guard'
    SourceLoc loc = guardTok.loc;

    // Parentheses are optional around the condition (Swift-style)
    bool hasParens = match(TokenKind::LParen);

    ExprPtr condition = parseExpression();
    if (!condition)
        return nullptr;

    if (hasParens && !expect(TokenKind::RParen, ")"))
        return nullptr;

    if (!expect(TokenKind::KwElse, "else"))
        return nullptr;

    StmtPtr elseBlock = parseStatement();
    if (!elseBlock)
        return nullptr;

    return std::make_unique<GuardStmt>(loc, std::move(condition), std::move(elseBlock));
}

/// @brief Parse a match statement (match expr { pattern => body; ... }).
/// @details Each arm consists of a pattern, optional guard (if condition), and a body
///          that is either a block or a single expression followed by a semicolon.
/// @return The parsed MatchStmt, or nullptr on error.
StmtPtr Parser::parseMatchStmt()
{
    Token matchTok = advance(); // consume 'match'
    SourceLoc loc = matchTok.loc;

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

        // Parse arm body (expression or block)
        if (check(TokenKind::LBrace))
        {
            // Block body - parse as block expression
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

} // namespace il::frontends::zia
