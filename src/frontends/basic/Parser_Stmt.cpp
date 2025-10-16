// File: src/frontends/basic/Parser_Stmt.cpp
// Purpose: Implements statement-level parsing routines for the BASIC parser.
// Key invariants: Relies on token buffer for lookahead.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// License: MIT; see LICENSE for details.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

void Parser::registerCoreParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordLet, &Parser::parseLetStatement);
    registry.registerHandler(TokenKind::KeywordFunction, &Parser::parseFunctionStatement);
    registry.registerHandler(TokenKind::KeywordSub, &Parser::parseSubStatement);
}

void Parser::noteProcedureName(std::string name)
{
    knownProcedures_.insert(std::move(name));
}

bool Parser::isKnownProcedureName(const std::string &name) const
{
    return knownProcedures_.find(name) != knownProcedures_.end();
}

/// @brief Parse a single statement based on the current token.
/// @param line Line number associated with the statement.
/// @return Parsed statement node or nullptr for unrecognized statements.
StmtPtr Parser::parseStatement(int line)
{
    const Token &tok = peek();
    std::string tokLexeme = tok.lexeme;
    auto tokLoc = tok.loc;
    if (tok.kind == TokenKind::Number)
    {
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           tok.loc,
                           static_cast<uint32_t>(tok.lexeme.size()),
                           "unexpected line number");
        }
        else
        {
            std::fprintf(stderr, "unexpected line number '%s'\n", tok.lexeme.c_str());
        }

        while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
        {
            consume();
        }
        return nullptr;
    }

    const auto kind = tok.kind;
    const auto [noArg, withLine] = statementRegistry().lookup(kind);
    if (noArg)
        return (this->*noArg)();
    if (withLine)
        return (this->*withLine)(line);
    if (tok.kind == TokenKind::Identifier && peek(1).kind == TokenKind::LParen)
    {
        std::string ident = tokLexeme;
        auto identLoc = tokLoc;
        auto expr = parseArrayOrVar();
        if (auto *callExpr = dynamic_cast<CallExpr *>(expr.get()))
        {
            auto stmt = std::make_unique<CallStmt>();
            stmt->loc = identLoc;
            stmt->call.reset(static_cast<CallExpr *>(expr.release()));
            return stmt;
        }
        if (emitter_)
        {
            std::string msg = std::string("unknown statement '") + ident + "'";
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           identLoc,
                           static_cast<uint32_t>(ident.size()),
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr, "unknown statement '%s'\n", ident.c_str());
        }
        while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
        {
            consume();
        }
        return nullptr;
    }
    if (tok.kind == TokenKind::Identifier && isKnownProcedureName(tokLexeme) &&
        peek(1).kind != TokenKind::LParen)
    {
        std::string ident = tokLexeme;
        auto nextTok = peek(1);
        auto diagLoc = nextTok.loc.isValid() ? nextTok.loc : tokLoc;
        uint32_t length = 1;
        if (emitter_)
        {
            std::string msg = "expected '(' after procedure name '" + ident + "'";
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           diagLoc,
                           length,
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr,
                         "expected '(' after procedure name '%s'\n",
                         ident.c_str());
        }
        while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
        {
            consume();
        }
        return nullptr;
    }
    if (emitter_)
    {
            std::string msg = std::string("unknown statement '") + tokLexeme + "'";
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           tokLoc,
                           static_cast<uint32_t>(tokLexeme.size()),
                           std::move(msg));
    }
    else
    {
        std::fprintf(stderr, "unknown statement '%s'\n", tok.lexeme.c_str());
    }

    while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
    {
        consume();
    }

    return nullptr;
}

/// @brief Check whether @p kind marks the beginning of a statement.
/// @param kind Token to examine.
/// @return True when a handler or structural keyword introduces a new statement.
bool Parser::isStatementStart(TokenKind kind) const
{
    switch (kind)
    {
        case TokenKind::KeywordAnd:
        case TokenKind::KeywordOr:
        case TokenKind::KeywordNot:
        case TokenKind::KeywordAndAlso:
        case TokenKind::KeywordOrElse:
            return false;
        case TokenKind::KeywordThen:
        case TokenKind::KeywordElse:
        case TokenKind::KeywordElseIf:
        case TokenKind::KeywordWend:
        case TokenKind::KeywordTo:
        case TokenKind::KeywordStep:
        case TokenKind::KeywordAs:
            return true;
        default:
            break;
    }
    return statementRegistry().contains(kind);
}
/// @brief Parse a LET assignment statement.
/// @return LetStmt with target and assigned expression.
/// @note Expects an '=' between target and expression.
StmtPtr Parser::parseLetStatement()
{
    auto loc = peek().loc;
    consume(); // LET
    auto target = parsePrimary();
    expect(TokenKind::Equal);
    auto e = parseExpression();
    auto stmt = std::make_unique<LetStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    stmt->expr = std::move(e);
    return stmt;
}
/// @brief Derive a BASIC type from an identifier suffix.
/// @param name Identifier to inspect.
/// @return Corresponding BASIC type; defaults to I64.
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

/// @brief Parse a type keyword following an AS clause.
/// @return BASIC type corresponding to the recognized keyword; defaults to Type::I64.
/// @details Supported keywords: BOOLEAN, INTEGER, DOUBLE, STRING. BOOLEAN is consumed directly
/// by keyword, while the others are matched as identifiers. If no supported keyword is present or
/// an unknown identifier is encountered, the token is treated as INTEGER semantics and Type::I64 is
/// returned without emitting diagnostics, leaving callers to rely on default typing rules.
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

/// @brief Parse a parenthesized parameter list.
/// @return Vector of parameters, possibly empty if no list is present.
/// @details Accepts comma-separated identifiers with optional trailing "()" to mark arrays and type
/// suffix characters to infer BASIC types. When no opening parenthesis is found the function
/// returns immediately without consuming tokens. Token mismatches are diagnosed via expect(),
/// allowing the caller to surface parser errors consistently.
std::vector<Param> Parser::parseParamList()
{
    std::vector<Param> params;
    if (!at(TokenKind::LParen))
        return params;
    consume(); // (
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

/// @brief Parse the header of a FUNCTION declaration.
/// @return FunctionDecl populated with name, return type, and parameters.
std::unique_ptr<FunctionDecl> Parser::parseFunctionHeader()
{
    auto loc = peek().loc;
    consume(); // FUNCTION
    Token nameTok = expect(TokenKind::Identifier);
    auto fn = std::make_unique<FunctionDecl>();
    fn->loc = loc;
    fn->name = nameTok.lexeme;
    fn->ret = typeFromSuffix(nameTok.lexeme);
    fn->params = parseParamList();
    return fn;
}

/// @brief Shared helper that parses procedure bodies terminated by END.
/// @param endKind Keyword expected after END to close the body.
/// @param body Destination vector for parsed statements.
/// @return Location of the END keyword token.
il::support::SourceLoc Parser::parseProcedureBody(TokenKind endKind, std::vector<StmtPtr> &body)
{
    auto ctx = statementSequencer();
    auto info = ctx.collectStatements(
        [&](int, il::support::SourceLoc)
        { return at(TokenKind::KeywordEnd) && peek(1).kind == endKind; },
        [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
        {
            consume();
            consume();
        },
        body);
    return info.loc;
}

/// @brief Parse statements comprising a function body.
/// @param fn FunctionDecl to populate with body statements.
/// @note Consumes tokens until reaching END FUNCTION.
void Parser::parseFunctionBody(FunctionDecl *fn)
{
    fn->endLoc = parseProcedureBody(TokenKind::KeywordFunction, fn->body);
}

/// @brief Parse a full FUNCTION declaration.
/// @return FunctionDecl statement node including body.
StmtPtr Parser::parseFunctionStatement()
{
    auto fn = parseFunctionHeader();
    noteProcedureName(fn->name);
    parseFunctionBody(fn.get());
    return fn;
}

/// @brief Parse a full SUB procedure declaration.
/// @return SubDecl statement node including body.
StmtPtr Parser::parseSubStatement()
{
    auto loc = peek().loc;
    consume(); // SUB
    Token nameTok = expect(TokenKind::Identifier);
    auto sub = std::make_unique<SubDecl>();
    sub->loc = loc;
    sub->name = nameTok.lexeme;
    sub->params = parseParamList();
    noteProcedureName(sub->name);
    parseProcedureBody(TokenKind::KeywordSub, sub->body);
    return sub;
}
} // namespace il::frontends::basic
