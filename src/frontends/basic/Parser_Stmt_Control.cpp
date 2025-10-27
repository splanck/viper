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
#include "frontends/basic/parse/StmtRegistry.hpp"

namespace il::frontends::basic
{

/// @brief Install the control-flow statement handlers required by BASIC.
///
/// @details The parser populates a registry with mappings from control-flow
///          keywords to lightweight handler lambdas.  When the front end
///          encounters a keyword token it consults this registry to discover the
///          appropriate parsing routine, keeping dispatch table-driven and easy
///          to extend.  Each registration captures the member function that
///          performs the actual parsing work.
///
/// @param registry Mutable registry that records keyword-to-parser mappings.
void Parser::registerControlFlowParsers(parse::StmtRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordIf,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseIfStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordSelect,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseSelectCaseStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordWhile,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseWhileStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordDo,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseDoStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordFor,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseForStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordNext,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseNextStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordExit,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseExitStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordGoto,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseGotoStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordGosub,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseGosubStatement));
                                 return true;
                             });
    registry.registerHandler(TokenKind::KeywordReturn,
                             [](parse::TokenStream &, parse::ASTBuilder &builder, parse::Diagnostics &)
                             {
                                 builder.setStatement(builder.call(&Parser::parseReturnStatement));
                                 return true;
                             });
}

} // namespace il::frontends::basic
