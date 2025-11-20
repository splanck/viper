//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Parser_Stmt_Control.cpp
// Purpose: Registers the control-flow statement parsers used by the BASIC front
//          end. The parser owns a registry that maps keywords to member-function
//          callbacks; this file installs the callbacks that recognise branching
//          and looping constructs so the parser can remain data-driven instead of
//          hard-coding dispatch logic in a large conditional.
// Key invariants: Registration order mirrors keyword definitions to keep error
//                 messages deterministic.
// Ownership/Lifetime: Registry entries borrow parser member functions; no
//                     additional resources are owned.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the control-flow registration routine for the BASIC parser.
/// @details BASIC supports a diverse set of control-flow keywords.  Rather than
///          emit a switch statement for each token, the parser keeps a registry
///          that associates keywords with the member function responsible for
///          parsing that statement.  This translation unit focuses solely on
///          populating that registry so the parsing code that consumes it stays
///          uncluttered.

#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"

namespace il::frontends::basic
{

/// @brief Install the control-flow statement handlers required by BASIC.
///
/// @details The parser populates a `StatementParserRegistry` with mappings from
///          control-flow keywords to the member functions that understand their
///          syntax.  When the front end encounters a keyword token it consults
///          this registry to discover the appropriate parsing routine, keeping
///          dispatch table-driven and easy to extend.  Each registration stores a
///          pointer-to-member; ownership of the actual parsing implementations
///          remains with `Parser`.
///
/// @param registry Mutable registry that records keyword-to-parser mappings.
void Parser::registerControlFlowParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordIf, &Parser::parseIfStatement);
    registry.registerHandler(TokenKind::KeywordSelect, &Parser::parseSelectCaseStatement);
    registry.registerHandler(TokenKind::KeywordWhile, &Parser::parseWhileStatement);
    registry.registerHandler(TokenKind::KeywordDo, &Parser::parseDoStatement);
    registry.registerHandler(TokenKind::KeywordFor, &Parser::parseForStatement);
    registry.registerHandler(TokenKind::KeywordNext, &Parser::parseNextStatement);
    registry.registerHandler(TokenKind::KeywordExit, &Parser::parseExitStatement);
    registry.registerHandler(TokenKind::KeywordGoto, &Parser::parseGotoStatement);
    registry.registerHandler(TokenKind::KeywordGosub, &Parser::parseGosubStatement);
    registry.registerHandler(TokenKind::KeywordReturn, &Parser::parseReturnStatement);
    registry.registerHandler(TokenKind::KeywordTry, &Parser::parseTryCatchStatement);
}

/// @brief Parse a TRY/CATCH statement with an optional catch variable.
///
/// TRY
///   ...
/// CATCH [ident]
///   ...
/// END TRY
StmtPtr Parser::parseTryCatchStatement()
{
    auto locTry = peek().loc;
    consume(); // TRY

    auto node = std::make_unique<TryCatchStmt>();
    node->loc = locTry;
    node->header.begin = locTry;

    // Collect TRY body until CATCH or END TRY
    auto ctx = statementSequencer();
    enum class Term
    {
        None,
        Catch,
        EndTry
    } term = Term::None;
    auto termInfo = ctx.collectStatements(
        [&](int, il::support::SourceLoc)
        {
            if (at(TokenKind::KeywordCatch))
                return true;
            if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordTry)
                return true;
            return false;
        },
        [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
        {
            if (at(TokenKind::KeywordCatch))
            {
                term = Term::Catch;
                node->header.end = peek().loc;
                consume(); // CATCH
            }
            else if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordTry)
            {
                term = Term::EndTry;
                node->header.end = peek().loc;
                consume(); // END
                consume(); // TRY
            }
        },
        node->tryBody);

    // Must have a CATCH
    if (term != Term::Catch)
    {
        emitError("B3201", termInfo.loc, "missing CATCH for TRY block");
        // We saw END TRY or EOF — return the node with just TRY body to keep progress.
        return node;
    }

    // Optional catch variable
    if (at(TokenKind::Identifier))
    {
        node->catchVar = CanonicalizeIdent(peek().lexeme);
        consume();
    }

    // Optional line breaks before CATCH body
    ctx.skipLineBreaks();

    // Collect CATCH body until END TRY
    bool sawEndTry = false;
    ctx.collectStatements(
        [&](int, il::support::SourceLoc)
        { return at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordTry; },
        [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
        {
            sawEndTry = true;
            consume(); // END
            consume(); // TRY
        },
        node->catchBody);

    if (!sawEndTry)
    {
        emitError("B3202", locTry, "missing END TRY to terminate TRY/CATCH");
    }

    return node;
}

} // namespace il::frontends::basic
