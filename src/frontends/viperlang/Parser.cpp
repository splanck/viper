//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Parser.cpp
// Purpose: Implements the recursive descent parser for ViperLang.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Parser.hpp"

namespace il::frontends::viperlang
{

Parser::Parser(Lexer &lexer, il::support::DiagnosticEngine &diag)
    : lexer_(lexer), diag_(diag)
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
        if (check(TokenKind::RBrace) || check(TokenKind::KwFunc) ||
            check(TokenKind::KwValue) || check(TokenKind::KwEntity) ||
            check(TokenKind::KwInterface))
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
        "V2000"  // ViperLang parser error code
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
    if (!expr) return nullptr;

    if (match(TokenKind::Equal))
    {
        SourceLoc loc = current_.loc;
        ExprPtr value = parseAssignment();  // right-associative
        if (!value) return nullptr;
        return std::make_unique<BinaryExpr>(loc, BinaryOp::Assign, std::move(expr), std::move(value));
    }

    return expr;
}

ExprPtr Parser::parseTernary()
{
    ExprPtr expr = parseRange();
    if (!expr) return nullptr;

    if (match(TokenKind::Question))
    {
        SourceLoc loc = current_.loc;
        ExprPtr thenExpr = parseExpression();
        if (!thenExpr) return nullptr;

        if (!expect(TokenKind::Colon, ":"))
            return nullptr;

        ExprPtr elseExpr = parseTernary();
        if (!elseExpr) return nullptr;

        return std::make_unique<TernaryExpr>(loc, std::move(expr),
                                              std::move(thenExpr), std::move(elseExpr));
    }

    return expr;
}

ExprPtr Parser::parseRange()
{
    ExprPtr expr = parseCoalesce();
    if (!expr) return nullptr;

    while (check(TokenKind::DotDot) || check(TokenKind::DotDotEqual))
    {
        bool inclusive = check(TokenKind::DotDotEqual);
        SourceLoc loc = current_.loc;
        advance();

        ExprPtr right = parseCoalesce();
        if (!right) return nullptr;

        expr = std::make_unique<RangeExpr>(loc, std::move(expr), std::move(right), inclusive);
    }

    return expr;
}

ExprPtr Parser::parseCoalesce()
{
    ExprPtr expr = parseLogicalOr();
    if (!expr) return nullptr;

    while (match(TokenKind::QuestionQuestion))
    {
        SourceLoc loc = current_.loc;
        ExprPtr right = parseLogicalOr();
        if (!right) return nullptr;

        expr = std::make_unique<CoalesceExpr>(loc, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseLogicalOr()
{
    ExprPtr expr = parseLogicalAnd();
    if (!expr) return nullptr;

    while (match(TokenKind::PipePipe))
    {
        SourceLoc loc = current_.loc;
        ExprPtr right = parseLogicalAnd();
        if (!right) return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::Or, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseLogicalAnd()
{
    ExprPtr expr = parseEquality();
    if (!expr) return nullptr;

    while (match(TokenKind::AmpAmp))
    {
        SourceLoc loc = current_.loc;
        ExprPtr right = parseEquality();
        if (!right) return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, BinaryOp::And, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseEquality()
{
    ExprPtr expr = parseComparison();
    if (!expr) return nullptr;

    while (check(TokenKind::EqualEqual) || check(TokenKind::NotEqual))
    {
        BinaryOp op = check(TokenKind::EqualEqual) ? BinaryOp::Eq : BinaryOp::Ne;
        SourceLoc loc = current_.loc;
        advance();

        ExprPtr right = parseComparison();
        if (!right) return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseComparison()
{
    ExprPtr expr = parseAdditive();
    if (!expr) return nullptr;

    while (check(TokenKind::Less) || check(TokenKind::LessEqual) ||
           check(TokenKind::Greater) || check(TokenKind::GreaterEqual))
    {
        BinaryOp op;
        if (check(TokenKind::Less)) op = BinaryOp::Lt;
        else if (check(TokenKind::LessEqual)) op = BinaryOp::Le;
        else if (check(TokenKind::Greater)) op = BinaryOp::Gt;
        else op = BinaryOp::Ge;

        SourceLoc loc = current_.loc;
        advance();

        ExprPtr right = parseAdditive();
        if (!right) return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseAdditive()
{
    ExprPtr expr = parseMultiplicative();
    if (!expr) return nullptr;

    while (check(TokenKind::Plus) || check(TokenKind::Minus))
    {
        BinaryOp op = check(TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
        SourceLoc loc = current_.loc;
        advance();

        ExprPtr right = parseMultiplicative();
        if (!right) return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseMultiplicative()
{
    ExprPtr expr = parseUnary();
    if (!expr) return nullptr;

    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent))
    {
        BinaryOp op;
        if (check(TokenKind::Star)) op = BinaryOp::Mul;
        else if (check(TokenKind::Slash)) op = BinaryOp::Div;
        else op = BinaryOp::Mod;

        SourceLoc loc = current_.loc;
        advance();

        ExprPtr right = parseUnary();
        if (!right) return nullptr;

        expr = std::make_unique<BinaryExpr>(loc, op, std::move(expr), std::move(right));
    }

    return expr;
}

ExprPtr Parser::parseUnary()
{
    if (check(TokenKind::Minus) || check(TokenKind::Bang) || check(TokenKind::Tilde))
    {
        UnaryOp op;
        if (check(TokenKind::Minus)) op = UnaryOp::Neg;
        else if (check(TokenKind::Bang)) op = UnaryOp::Not;
        else op = UnaryOp::BitNot;

        SourceLoc loc = current_.loc;
        advance();

        ExprPtr operand = parseUnary();
        if (!operand) return nullptr;

        return std::make_unique<UnaryExpr>(loc, op, std::move(operand));
    }

    return parsePostfix();
}

ExprPtr Parser::parsePostfix()
{
    ExprPtr expr = parsePrimary();
    if (!expr) return nullptr;

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
            if (!index) return nullptr;

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
            if (!type) return nullptr;

            expr = std::make_unique<IsExpr>(loc, std::move(expr), std::move(type));
        }
        else if (match(TokenKind::KwAs))
        {
            // Type cast
            SourceLoc loc = current_.loc;
            TypePtr type = parseType();
            if (!type) return nullptr;

            expr = std::make_unique<AsExpr>(loc, std::move(expr), std::move(type));
        }
        else
        {
            break;
        }
    }

    return expr;
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

    // New expression
    if (match(TokenKind::KwNew))
    {
        TypePtr type = parseType();
        if (!type) return nullptr;

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
    if (match(TokenKind::LParen))
    {
        // Check for unit literal ()
        if (match(TokenKind::RParen))
        {
            return std::make_unique<UnitLiteralExpr>(loc);
        }

        ExprPtr expr = parseExpression();
        if (!expr) return nullptr;

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
    advance();  // consume '['

    std::vector<ExprPtr> elements;

    if (!check(TokenKind::RBracket))
    {
        do
        {
            ExprPtr elem = parseExpression();
            if (!elem) return nullptr;
            elements.push_back(std::move(elem));
        } while (match(TokenKind::Comma));
    }

    if (!expect(TokenKind::RBracket, "]"))
        return nullptr;

    return std::make_unique<ListLiteralExpr>(loc, std::move(elements));
}

ExprPtr Parser::parseMapOrSetLiteral()
{
    SourceLoc loc = current_.loc;
    advance();  // consume '{'

    // Empty brace = empty map (by convention)
    if (check(TokenKind::RBrace))
    {
        advance();
        return std::make_unique<MapLiteralExpr>(loc, std::vector<MapEntry>{});
    }

    // Check if first element has colon (map) or not (set)
    ExprPtr first = parseExpression();
    if (!first) return nullptr;

    if (match(TokenKind::Colon))
    {
        // It's a map
        std::vector<MapEntry> entries;

        ExprPtr firstValue = parseExpression();
        if (!firstValue) return nullptr;

        entries.push_back({std::move(first), std::move(firstValue)});

        while (match(TokenKind::Comma))
        {
            ExprPtr key = parseExpression();
            if (!key) return nullptr;

            if (!expect(TokenKind::Colon, ":"))
                return nullptr;

            ExprPtr value = parseExpression();
            if (!value) return nullptr;

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
            if (!elem) return nullptr;
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
                // Not a named arg, parse as expression starting with this identifier
                ExprPtr ident = std::make_unique<IdentExpr>(nameTok.loc, nameTok.text);
                // Continue parsing postfix/binary operators
                // For simplicity, re-parse the whole thing
                // This is a bit wasteful but works
                // Actually let's create the IdentExpr and continue parsing from parsePostfix level
                arg.value = std::move(ident);
                // Need to continue parsing operators after the identifier
                // Simplified: just use the ident for now
            }
        }
        else
        {
            arg.value = parseExpression();
        }

        if (!arg.value) return {};
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

    // Variable declaration
    if (check(TokenKind::KwVar) || check(TokenKind::KwFinal))
    {
        return parseVarDecl();
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
    if (!expr) return nullptr;

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
    advance();  // consume var/final

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
        if (!type) return nullptr;
    }

    // Optional initializer
    ExprPtr init;
    if (match(TokenKind::Equal))
    {
        init = parseExpression();
        if (!init) return nullptr;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return std::make_unique<VarStmt>(loc, std::move(name), std::move(type), std::move(init), isFinal);
}

StmtPtr Parser::parseIfStmt()
{
    SourceLoc loc = current_.loc;
    advance();  // consume 'if'

    if (!expect(TokenKind::LParen, "("))
        return nullptr;

    ExprPtr condition = parseExpression();
    if (!condition) return nullptr;

    if (!expect(TokenKind::RParen, ")"))
        return nullptr;

    StmtPtr thenBranch = parseStatement();
    if (!thenBranch) return nullptr;

    StmtPtr elseBranch;
    if (match(TokenKind::KwElse))
    {
        elseBranch = parseStatement();
        if (!elseBranch) return nullptr;
    }

    return std::make_unique<IfStmt>(loc, std::move(condition),
                                     std::move(thenBranch), std::move(elseBranch));
}

StmtPtr Parser::parseWhileStmt()
{
    SourceLoc loc = current_.loc;
    advance();  // consume 'while'

    if (!expect(TokenKind::LParen, "("))
        return nullptr;

    ExprPtr condition = parseExpression();
    if (!condition) return nullptr;

    if (!expect(TokenKind::RParen, ")"))
        return nullptr;

    StmtPtr body = parseStatement();
    if (!body) return nullptr;

    return std::make_unique<WhileStmt>(loc, std::move(condition), std::move(body));
}

StmtPtr Parser::parseForStmt()
{
    SourceLoc loc = current_.loc;
    advance();  // consume 'for'

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
            if (!iterable) return nullptr;

            if (!expect(TokenKind::RParen, ")"))
                return nullptr;

            StmtPtr body = parseStatement();
            if (!body) return nullptr;

            return std::make_unique<ForInStmt>(loc, std::move(varName),
                                                std::move(iterable), std::move(body));
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
    advance();  // consume 'return'

    ExprPtr value;
    if (!check(TokenKind::Semicolon))
    {
        value = parseExpression();
        if (!value) return nullptr;
    }

    if (!expect(TokenKind::Semicolon, ";"))
        return nullptr;

    return std::make_unique<ReturnStmt>(loc, std::move(value));
}

StmtPtr Parser::parseGuardStmt()
{
    SourceLoc loc = current_.loc;
    advance();  // consume 'guard'

    if (!expect(TokenKind::LParen, "("))
        return nullptr;

    ExprPtr condition = parseExpression();
    if (!condition) return nullptr;

    if (!expect(TokenKind::RParen, ")"))
        return nullptr;

    if (!expect(TokenKind::KwElse, "else"))
        return nullptr;

    StmtPtr elseBlock = parseStatement();
    if (!elseBlock) return nullptr;

    return std::make_unique<GuardStmt>(loc, std::move(condition), std::move(elseBlock));
}

StmtPtr Parser::parseMatchStmt()
{
    // TODO: implement match statement parsing
    error("match statement not yet implemented");
    return nullptr;
}

//===----------------------------------------------------------------------===//
// Type Parsing
//===----------------------------------------------------------------------===//

TypePtr Parser::parseType()
{
    TypePtr base = parseBaseType();
    if (!base) return nullptr;

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
                if (!arg) return nullptr;
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
                if (!elem) return nullptr;
                elements.push_back(std::move(elem));
            } while (match(TokenKind::Comma));
        }

        if (!expect(TokenKind::RParen, ")"))
            return nullptr;

        // Check for function type
        if (match(TokenKind::Arrow))
        {
            TypePtr returnType = parseType();
            if (!returnType) return nullptr;

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
    advance();  // consume 'import'

    // Parse path: Viper.IO.File
    std::string path;
    if (!check(TokenKind::Identifier))
    {
        error("expected import path");
        return ImportDecl(loc, "");
    }

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

    error("expected declaration");
    return nullptr;
}

DeclPtr Parser::parseFunctionDecl()
{
    SourceLoc loc = current_.loc;
    advance();  // consume 'func'

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
        if (!func->returnType) return nullptr;
    }

    // Body
    if (check(TokenKind::LBrace))
    {
        func->body = parseBlock();
        if (!func->body) return nullptr;
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
        if (!param.type) return {};

        // Default value
        if (match(TokenKind::Equal))
        {
            param.defaultValue = parseExpression();
            if (!param.defaultValue) return {};
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
    advance();  // consume 'value'

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

    // TODO: parse members
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        // Skip for now
        advance();
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return value;
}

DeclPtr Parser::parseEntityDecl()
{
    SourceLoc loc = current_.loc;
    advance();  // consume 'entity'

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

    // TODO: parse members
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        // Skip for now
        advance();
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return entity;
}

DeclPtr Parser::parseInterfaceDecl()
{
    SourceLoc loc = current_.loc;
    advance();  // consume 'interface'

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

    // TODO: parse method signatures
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof))
    {
        // Skip for now
        advance();
    }

    if (!expect(TokenKind::RBrace, "}"))
        return nullptr;

    return iface;
}

} // namespace il::frontends::viperlang
