//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the registration glue that wires BASIC control-flow keywords to
// their parsing routines.  Centralising the table-building logic here keeps the
// main parser implementation focused on grammar productions while making the
// registration policy easy to audit.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Registers control-flow statement parsers for the BASIC front end.
/// @details The parser installs member-function handlers for each keyword that
///          introduces a control-flow construct so that the registry can
///          dispatch to the appropriate parsing routine during tokenisation.

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/Parser_Stmt_ControlHelpers.hpp"

namespace il::frontends::basic
{

/// @brief Install all control-flow statement parsers into the registry.
/// @details Walks the set of control-flow keywords supported by the language
///          and binds each one to its associated parser member function.  The
///          registry keeps the associations so the parser can dispatch by
///          keyword at runtime without large conditional chains, while the
///          parser retains ownership of the implementations themselves.
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

