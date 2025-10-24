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
}

} // namespace il::frontends::basic
