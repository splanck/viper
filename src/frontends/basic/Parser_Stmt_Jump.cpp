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
#include "frontends/basic/ast/StmtNodesAll.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace il::frontends::basic
{

/// @brief Parse a `GOTO <line>` statement.
/// @details The routine expects the current token stream to be positioned at the
///          `GOTO` keyword.  It consumes the keyword, parses the trailing line
///          label (numeric or named), and materialises a @c GotoStmt containing
///          the resolved destination together with the originating source
///          location.  Errors emit diagnostics that describe the missing label or
///          number before recovering to the next statement boundary.
/// @return Owned AST node describing the goto statement.
StmtPtr Parser::parseGotoStatement()
{
    auto loc = peek().loc;
    consume(); // GOTO
    int target = 0;
    if (at(TokenKind::Identifier))
    {
        Token targetTok = consume();
        target = ensureLabelNumber(targetTok.lexeme);
        noteNamedLabelReference(targetTok, target);
    }
    else if (at(TokenKind::Number))
    {
        Token targetTok = consume();
        target = std::atoi(targetTok.lexeme.c_str());
        noteNumericLabelUsage(target);
    }
    else
    {
        Token unexpected = peek();
        if (emitter_)
        {
            std::string msg = "expected label or number after GOTO";
            uint32_t length = unexpected.lexeme.empty()
                                   ? 1u
                                   : static_cast<uint32_t>(unexpected.lexeme.size());
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           unexpected.loc,
                           length,
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr, "expected label or number after GOTO\n");
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
/// @details After consuming the `GOSUB` keyword the parser requires either a
///          numeric literal or a named label that identifies the subroutine
///          entry line.  The resulting @c GosubStmt records both the call-site
///          location and the numeric return address so later passes can emit the
///          appropriate frame setup. Input validation mirrors
///          @ref parseGotoStatement to guarantee consistent diagnostics.
/// @return Owned AST node describing the gosub statement.
StmtPtr Parser::parseGosubStatement()
{
    auto loc = peek().loc;
    consume(); // GOSUB
    int target = 0;
    if (at(TokenKind::Identifier))
    {
        Token targetTok = consume();
        target = ensureLabelNumber(targetTok.lexeme);
        noteNamedLabelReference(targetTok, target);
    }
    else if (at(TokenKind::Number))
    {
        Token targetTok = consume();
        target = std::atoi(targetTok.lexeme.c_str());
        noteNumericLabelUsage(target);
    }
    else
    {
        Token unexpected = peek();
        if (emitter_)
        {
            std::string msg = "expected label or number after GOSUB";
            uint32_t length = unexpected.lexeme.empty()
                                   ? 1u
                                   : static_cast<uint32_t>(unexpected.lexeme.size());
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           unexpected.loc,
                           length,
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr, "expected label or number after GOSUB\n");
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
