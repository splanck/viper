//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Parser_Stmt.cpp
/// @brief Statement parsing implementation for the ViperLang parser.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Parser.hpp"

namespace il::frontends::viperlang
{

//===----------------------------------------------------------------------===//
// Statement Parsing
//===----------------------------------------------------------------------===//

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
    Token lbraceTok;
    if (!expect(TokenKind::LBrace, "{", &lbraceTok))
        return nullptr;
    SourceLoc loc = lbraceTok.loc;

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

StmtPtr Parser::parseIfStmt()
{
    Token ifTok = advance(); // consume 'if'
    SourceLoc loc = ifTok.loc;

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
    Token whileTok = advance(); // consume 'while'
    SourceLoc loc = whileTok.loc;

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
    Token forTok = advance(); // consume 'for'
    SourceLoc loc = forTok.loc;

    bool hasParen = match(TokenKind::LParen);

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

StmtPtr Parser::parseGuardStmt()
{
    Token guardTok = advance(); // consume 'guard'
    SourceLoc loc = guardTok.loc;

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

} // namespace il::frontends::viperlang
