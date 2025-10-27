//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Parser_Stmt_Jump.cpp
// Purpose: Parse BASIC control-transfer statements (GOTO, GOSUB, RETURN).
// Key invariants: Each parser consumes tokens in lock-step with the lexer,
//                 produces heap-allocated AST nodes, and records source
//                 locations for later diagnostics.
// Ownership/Lifetime: Returned AST nodes use std::unique_ptr semantics; the
//                     parser retains no ownership once the node is returned to
//                     the caller.
// Links: docs/basic-language.md#statements, docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements jump-oriented BASIC statement parsers.
/// @details Provides the parsing routines for GOTO, GOSUB, and RETURN statements
///          and returns heap-allocated AST nodes describing the parsed constructs.

#include "frontends/basic/Parser.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace il::frontends::basic
{

/// @brief Parse a `GOTO <line>` statement.
/// @details The routine expects the current token stream to be positioned at the
///          `GOTO` keyword.  It consumes the keyword, parses the trailing target
///          (either a numeric line number or a named label translated through
///          @ref ensureLabelNumber), and materialises a @c GotoStmt containing
///          the resolved destination alongside the originating source location.
///          Errors (such as a missing target) trigger diagnostics before
///          attempting statement-boundary recovery.
/// @return Owned AST node describing the goto statement.
StmtPtr Parser::parseGotoStatement()
{
    Token kwTok = consume(); // GOTO
    auto loc = kwTok.loc;
    int target = 0;
    if (at(TokenKind::Number))
    {
        Token targetTok = consume();
        target = std::atoi(targetTok.lexeme.c_str());
        noteNumericLabelUsage(target);
    }
    else if (at(TokenKind::Identifier))
    {
        Token targetTok = consume();
        target = ensureLabelNumber(targetTok.lexeme);
        noteNamedLabelReference(targetTok, target);
    }
    else
    {
        const Token &unexpected = peek();
        auto diagLoc = unexpected.loc.hasLine() ? unexpected.loc : kwTok.loc;
        auto length = unexpected.lexeme.empty() ? 1u
                                                : static_cast<unsigned>(unexpected.lexeme.size());
        if (emitter_)
        {
            std::string msg = "expected label or number after " + kwTok.lexeme;
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           diagLoc,
                           length,
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr, "expected label or number after %s\n", kwTok.lexeme.c_str());
        }
        syncToStmtBoundary();
        return nullptr;
    }
    auto stmt = std::make_unique<GotoStmt>();
    stmt->loc = loc;
    stmt->target = target;
    return stmt;
}

/// @brief Parse a `GOSUB <line>` statement.
/// @details After consuming the `GOSUB` keyword the parser accepts either a
///          numeric literal or a named label identifying the subroutine entry
///          point.  The resulting @c GosubStmt records both the call-site
///          location and the resolved target so later passes can emit the
///          appropriate frame setup.  Input validation mirrors
///          @ref parseGotoStatement to guarantee consistent diagnostics.
/// @return Owned AST node describing the gosub statement.
StmtPtr Parser::parseGosubStatement()
{
    Token kwTok = consume(); // GOSUB
    auto loc = kwTok.loc;
    int target = 0;
    if (at(TokenKind::Number))
    {
        Token targetTok = consume();
        target = std::atoi(targetTok.lexeme.c_str());
        noteNumericLabelUsage(target);
    }
    else if (at(TokenKind::Identifier))
    {
        Token targetTok = consume();
        target = ensureLabelNumber(targetTok.lexeme);
        noteNamedLabelReference(targetTok, target);
    }
    else
    {
        const Token &unexpected = peek();
        auto diagLoc = unexpected.loc.hasLine() ? unexpected.loc : kwTok.loc;
        auto length = unexpected.lexeme.empty() ? 1u
                                                : static_cast<unsigned>(unexpected.lexeme.size());
        if (emitter_)
        {
            std::string msg = "expected label or number after " + kwTok.lexeme;
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           diagLoc,
                           length,
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr, "expected label or number after %s\n", kwTok.lexeme.c_str());
        }
        syncToStmtBoundary();
        return nullptr;
    }
    auto stmt = std::make_unique<GosubStmt>();
    stmt->loc = loc;
    stmt->targetLine = target;
    return stmt;
}

/// @brief Parse a `RETURN [expr]` statement.
/// @details Consumes the `RETURN` keyword, captures the current source
///          location, and optionally parses a trailing expression that supplies
///          a return value when present.  Parsing halts at statement separators
///          (`:`, end-of-line, or end-of-file) so chained statements are left in
///          the token buffer for subsequent parsers.  The resulting @c ReturnStmt
///          carries either a populated expression or a null pointer to indicate
///          a void-style return.
/// @return Owned AST node describing the return statement.
StmtPtr Parser::parseReturnStatement()
{
    auto loc = peek().loc;
    consume(); // RETURN
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->loc = loc;
    if (!at(TokenKind::EndOfLine) && !at(TokenKind::EndOfFile) && !at(TokenKind::Colon))
        stmt->value = parseExpression();
    return stmt;
}

} // namespace il::frontends::basic
