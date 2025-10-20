// File: src/frontends/basic/Parser_Stmt.cpp
// Purpose: Implements statement-level parsing routines for the BASIC parser.
// Key invariants: Relies on token buffer for lookahead.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// License: MIT; see LICENSE for details.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include <cstdio>
#include <string>
#include <utility>

namespace il::frontends::basic
{

void Parser::registerCoreParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordLet, &Parser::parseLetStatement);
    registry.registerHandler(TokenKind::KeywordFunction, &Parser::parseFunctionStatement);
    registry.registerHandler(TokenKind::KeywordSub, &Parser::parseSubStatement);
}

void Parser::registerOopParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordClass, &Parser::parseClassDecl);
    registry.registerHandler(TokenKind::KeywordType, &Parser::parseTypeDecl);
    registry.registerHandler(TokenKind::KeywordDelete, &Parser::parseDeleteStatement);
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
/// @note Recovery synchronizes on EndOfLine and EndOfFile tokens to prevent
/// cascading diagnostics when encountering unexpected input.
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

        // Synchronize on end-of-line/end-of-file terminators before continuing.
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
        // Synchronize on statement separators to resume parsing after diagnostics.
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

    // Recovery: skip tokens until the next newline boundary or EOF marker.
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
} // namespace il::frontends::basic
