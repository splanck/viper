//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Parser_Stmt.cpp
// Purpose: Implements the statement dispatch loop for the BASIC parser.
// Key invariants: Relies on the statement registry to map leading tokens to
//                 parselets and synchronises recovery at ':' and end-of-line.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace il::frontends::basic
{

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
/// @details Error recovery consumes tokens until the next statement boundary
///          marked by ':' or a line terminator (EOL/EOF). This keeps chained
///          statements aligned with the StatementSequencer terminators.
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

} // namespace il::frontends::basic

