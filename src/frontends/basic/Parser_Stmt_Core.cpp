//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Parser_Stmt_Core.cpp
// Purpose: Implement parsing routines for core BASIC statements such as LET and procedure declarations.
// Key invariants: Maintains the parser's registry of known procedures so CALL statements without parentheses can still be
//                 resolved and ensures assignment targets honour BASIC's typing conventions.
// Ownership/Lifetime: Parser allocates AST nodes with std::unique_ptr and transfers ownership to the caller.
// Links: docs/codemap.md, docs/basic-language.md#statements
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

/// Remember that a procedure with @p name was encountered.
void Parser::noteProcedureName(std::string name)
{
    knownProcedures_.insert(std::move(name));
}

/// Check whether @p name has been recorded previously.
bool Parser::isKnownProcedureName(const std::string &name) const
{
    return knownProcedures_.find(name) != knownProcedures_.end();
}

/// Parse a LET assignment statement when present.
Parser::StmtResult Parser::parseLet()
{
    if (!at(TokenKind::KeywordLet))
        return std::nullopt;
    auto stmt = parseLetStatement();
    return StmtResult(std::move(stmt));
}

/// Parse a procedure or method call statement when possible.
Parser::StmtResult Parser::parseCall(int)
{
    if (!at(TokenKind::Identifier))
        return std::nullopt;
    const Token identTok = peek();
    const Token nextTok = peek(1);
    if (nextTok.kind == TokenKind::Dot)
    {
        const Token t2 = peek(2);
        const Token t3 = peek(3);
        if (t2.kind == TokenKind::Identifier && t3.kind == TokenKind::LParen)
        {
            auto expr = parseExpression(/*min_prec=*/0);
            if (expr && dynamic_cast<MethodCallExpr *>(expr.get()) != nullptr)
            {
                auto stmt = std::make_unique<CallStmt>();
                stmt->loc = identTok.loc;
                stmt->call = std::move(expr);
                return StmtResult(std::move(stmt));
            }
            reportUnknownStatement(identTok);
            resyncAfterError();
            return StmtResult(StmtPtr{});
        }
        return std::nullopt;
    }
    if (nextTok.kind != TokenKind::LParen)
    {
        if (isKnownProcedureName(identTok.lexeme))
        {
            reportMissingCallParenthesis(identTok, nextTok);
            resyncAfterError();
            return StmtResult(StmtPtr{});
        }
        return std::nullopt;
    }

    auto expr = parseArrayOrVar();
    if (expr && dynamic_cast<CallExpr *>(expr.get()) != nullptr)
    {
        auto stmt = std::make_unique<CallStmt>();
        stmt->loc = identTok.loc;
        stmt->call = std::move(expr);
        return StmtResult(std::move(stmt));
    }
    reportInvalidCallExpression(identTok);
    resyncAfterError();
    return StmtResult(StmtPtr{});
}

/// Emit diagnostic when a procedure call omits its opening parenthesis.
void Parser::reportMissingCallParenthesis(const Token &identTok, const Token &nextTok)
{
    auto diagLoc = nextTok.loc.hasLine() ? nextTok.loc : identTok.loc;
    uint32_t length = nextTok.lexeme.empty() ? 1 : static_cast<uint32_t>(nextTok.lexeme.size());
    if (emitter_)
    {
        std::string message = "expected '(' after procedure name '" + identTok.lexeme + "' in procedure call statement";
        emitter_->emit(il::support::Severity::Error, "B0001", diagLoc, length, std::move(message));
    }
    else
    {
        std::fprintf(stderr, "expected '(' after procedure name '%s' in procedure call statement\n", identTok.lexeme.c_str());
    }
}

/// Emit diagnostic when an identifier cannot be interpreted as a statement.
void Parser::reportInvalidCallExpression(const Token &identTok)
{
    if (emitter_)
    {
        std::string message = "expected procedure call after identifier '" + identTok.lexeme + "'";
        emitter_->emit(il::support::Severity::Error,
                       "B0001",
                       identTok.loc,
                       static_cast<uint32_t>(identTok.lexeme.size()),
                       std::move(message));
    }
    else
    {
        std::fprintf(stderr, "expected procedure call after identifier '%s'\n", identTok.lexeme.c_str());
    }
}

/// Parse a `LET` assignment statement.
StmtPtr Parser::parseLetStatement()
{
    auto loc = peek().loc;
    consume();
    auto target = parsePrimary();
    expect(TokenKind::Equal);
    auto e = parseExpression();
    auto stmt = std::make_unique<LetStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    stmt->expr = std::move(e);
    return stmt;
}

/// Derive a BASIC type from an identifier suffix.
Type Parser::typeFromSuffix(std::string_view name)
{
    if (!name.empty())
    {
        char c = name.back();
        switch (c)
        {
            case '#':
            case '!':
                return Type::F64;
            case '$':
                return Type::Str;
            case '%':
            case '&':
                return Type::I64;
            default:
                break;
        }
    }
    return Type::I64;
}

/// Parse a type keyword following an `AS` clause.
Type Parser::parseTypeKeyword()
{
    if (at(TokenKind::KeywordBoolean))
    {
        consume();
        return Type::Bool;
    }
    if (at(TokenKind::Identifier))
    {
        std::string name = peek().lexeme;
        consume();
        if (name == "INTEGER")
            return Type::I64;
        if (name == "DOUBLE")
            return Type::F64;
        if (name == "SINGLE")
            return Type::F64;
        if (name == "STRING")
            return Type::Str;
    }
    return Type::I64;
}

/// Parse a parenthesised parameter list if present.
std::vector<Param> Parser::parseParamList()
{
    std::vector<Param> params;
    if (!at(TokenKind::LParen))
        return params;
    consume();
    if (at(TokenKind::RParen))
    {
        consume();
        return params;
    }
    while (true)
    {
        Token id = expect(TokenKind::Identifier);
        Param p;
        p.loc = id.loc;
        p.name = id.lexeme;
        p.type = typeFromSuffix(id.lexeme);
        if (at(TokenKind::LParen))
        {
            consume();
            expect(TokenKind::RParen);
            p.is_array = true;
        }
        params.push_back(std::move(p));
        if (at(TokenKind::Comma))
        {
            consume();
            continue;
        }
        break;
    }
    expect(TokenKind::RParen);
    return params;
}

/// Parse a complete `FUNCTION` procedure declaration.
StmtPtr Parser::parseFunctionStatement()
{
    auto func = parseFunctionHeader();
    if (func->explicitRetType != BasicType::Unknown)
    {
        switch (func->explicitRetType)
        {
            case BasicType::Int:
                func->ret = Type::I64;
                break;
            case BasicType::Float:
                func->ret = Type::F64;
                break;
            case BasicType::String:
                func->ret = Type::Str;
                break;
            case BasicType::Void:
                func->ret = Type::I64;
                break;
            case BasicType::Unknown:
                break;
        }
    }
    noteProcedureName(func->name);
    parseProcedureBody(TokenKind::KeywordFunction, func->body);
    return func;
}

/// Parse a complete `SUB` procedure declaration.
StmtPtr Parser::parseSubStatement()
{
    auto loc = peek().loc;
    consume();
    Token nameTok = expect(TokenKind::Identifier);
    auto sub = std::make_unique<SubDecl>();
    sub->loc = loc;
    sub->name = nameTok.lexeme;
    sub->params = parseParamList();
    if (at(TokenKind::KeywordAs))
    {
        Token asTok = consume();
        if (!at(TokenKind::EndOfLine) && !at(TokenKind::EndOfFile))
            consume();
        if (emitter_)
        {
            std::string message = "SUB cannot have 'AS <TYPE>'";
            emitter_->emit(il::support::Severity::Error,
                           "B4007",
                           asTok.loc,
                           static_cast<uint32_t>(asTok.lexeme.size()),
                           std::move(message));
        }
        else
        {
            std::fprintf(stderr, "SUB cannot have 'AS <TYPE>'\n");
        }
    }
    noteProcedureName(sub->name);
    parseProcedureBody(TokenKind::KeywordSub, sub->body);
    return sub;
}

} // namespace il::frontends::basic

