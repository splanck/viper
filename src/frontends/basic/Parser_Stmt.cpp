//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Parser_Stmt.cpp
// Purpose: Implement statement-level parsing routines for the BASIC front end.
// Key invariants: Relies on the token buffer for deterministic lookahead and
//                 records discovered procedures so forward references can be
//                 recognised.
// Ownership/Lifetime: Parser owns tokens produced by the lexer and returns
//                     statements as std::unique_ptr nodes that the caller
//                     manages.
// Links: docs/codemap.md, docs/basic-language.md#statements
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "viper/diag/BasicDiag.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

/// @brief Register the minimal statement parsers required for core BASIC.
///
/// @details Installs handlers for assignment statements and procedure
///          declarations.  The registry keeps pointers-to-member functions so
///          dispatch remains table-driven.  Centralising the registration here
///          keeps @ref Parser::parseStatement focused on lookup rather than the
///          list of supported statements.
///
/// @param registry Dispatcher that maps statement-starting tokens to Parser
///        member functions.
void Parser::registerCoreParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordLet, &Parser::parseLetStatement);
    registry.registerHandler(TokenKind::KeywordFunction, &Parser::parseFunctionStatement);
    registry.registerHandler(TokenKind::KeywordSub, &Parser::parseSubStatement);
}

/// @brief Register object-oriented statement parsers when the feature set is enabled.
///
/// @details Adds handlers for class/type declarations and delete statements so
///          source files that opt into OOP extensions can parse the additional
///          constructs.  The core registry remains unchanged when these
///          keywords are absent.
///
/// @param registry Dispatcher that records keyword-to-handler mappings.
void Parser::registerOopParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordClass, &Parser::parseClassDecl);
    registry.registerHandler(TokenKind::KeywordType, &Parser::parseTypeDecl);
    registry.registerHandler(TokenKind::KeywordDelete, &Parser::parseDeleteStatement);
}

/// @brief Parse a single BASIC statement based on the current token.
///
/// @details Delegates parsing to specialised helpers that either return a
///          constructed statement or report an error before invoking
///          @ref resyncAfterError for recovery.  The dispatcher keeps the
///          per-statement logic isolated, improving readability and making the
///          recovery strategy consistent across statement kinds.
///
/// @param line One-based line number attached to the statement from the
///        original source listing.
/// @return Owned AST node on success; @c nullptr when recovery is required.
StmtPtr Parser::parseStatement(int line)
{
    if (auto handled = parseLeadingLineNumberError())
        return std::move(*handled);
    if (auto stmt = parseIf(line))
        return std::move(*stmt);
    if (auto stmt = parseSelect(line))
        return std::move(*stmt);
    if (auto stmt = parseFor(line))
        return std::move(*stmt);
    if (auto stmt = parseWhile(line))
        return std::move(*stmt);
    if (auto stmt = parseLet())
        return std::move(*stmt);
    if (auto stmt = parseImplicitLet())
        return std::move(*stmt);
    if (auto stmt = parseCall(line))
        return std::move(*stmt);
    if (auto stmt = parseRegisteredStatement(line))
        return std::move(*stmt);

    Token offendingTok = peek();
    reportUnknownStatement(offendingTok);
    resyncAfterError();
    while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
    {
        consume();
    }
    return nullptr;
}

Parser::StmtResult Parser::parseRegisteredStatement(int line)
{
    const Token &tok = peek();
    const auto [noArg, withLine] = statementRegistry().lookup(tok.kind);
    if (noArg)
    {
        auto stmt = (this->*noArg)();
        return StmtResult(std::move(stmt));
    }
    if (withLine)
    {
        auto stmt = (this->*withLine)(line);
        return StmtResult(std::move(stmt));
    }
    return std::nullopt;
}

Parser::StmtResult Parser::parseIf(int line)
{
    if (!at(TokenKind::KeywordIf))
        return std::nullopt;
    auto stmt = parseIfStatement(line);
    return StmtResult(std::move(stmt));
}

Parser::StmtResult Parser::parseSelect(int line)
{
    static_cast<void>(line);
    if (!at(TokenKind::KeywordSelect))
        return std::nullopt;
    auto stmt = parseSelectCaseStatement();
    return StmtResult(std::move(stmt));
}

Parser::StmtResult Parser::parseFor(int line)
{
    static_cast<void>(line);
    if (!at(TokenKind::KeywordFor))
        return std::nullopt;
    auto stmt = parseForStatement();
    return StmtResult(std::move(stmt));
}

Parser::StmtResult Parser::parseWhile(int line)
{
    static_cast<void>(line);
    if (!at(TokenKind::KeywordWhile))
        return std::nullopt;
    auto stmt = parseWhileStatement();
    return StmtResult(std::move(stmt));
}

Parser::StmtResult Parser::parseLeadingLineNumberError()
{
    if (!at(TokenKind::Number))
        return std::nullopt;

    reportUnexpectedLineNumber(peek());
    resyncAfterError();
    return StmtResult(StmtPtr{});
}

void Parser::reportUnexpectedLineNumber(const Token &tok)
{
    if (emitter_)
    {
        auto diagId = diag::BasicDiag::UnexpectedLineNumber;
        emitter_->emit(diag::getSeverity(diagId),
                       std::string(diag::getCode(diagId)),
                       tok.loc,
                       static_cast<uint32_t>(tok.lexeme.size()),
                       diag::formatMessage(diagId,
                                           std::initializer_list<diag::Replacement>{diag::Replacement{"token", tok.lexeme}}));
    }
    else
    {
        std::fprintf(stderr, "unexpected line number '%s' before statement\n", tok.lexeme.c_str());
    }
}

void Parser::reportUnknownStatement(const Token &tok)
{
    if (emitter_)
    {
        auto diagId = diag::BasicDiag::UnknownStatement;
        emitter_->emit(diag::getSeverity(diagId),
                       std::string(diag::getCode(diagId)),
                       tok.loc,
                       static_cast<uint32_t>(tok.lexeme.size()),
                       diag::formatMessage(diagId,
                                           std::initializer_list<diag::Replacement>{diag::Replacement{"token", tok.lexeme}}));
    }
    else
    {
        std::fprintf(stderr,
                     "unknown statement '%s'; expected keyword or procedure call\n",
                     tok.lexeme.c_str());
    }
}

void Parser::resyncAfterError()
{
    syncToStmtBoundary();
}

/// @brief Check whether @p kind marks the beginning of a statement.
///
/// @details Some keywords are used both as statement leaders and as infix
///          operators.  The parser explicitly filters out logical operators and
///          recognises structural keywords such as THEN and ELSE so the caller
///          can treat them as implicit statement boundaries when splitting
///          colon-separated sequences.
///
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
    if (at(TokenKind::KeywordAs))
    {
        consume();
        fn->explicitRetType = parseBasicType();
    }
    return fn;
}

/// @brief Parse a sequence of statements terminated by `END <keyword>`.
///
/// @details Delegates to @ref StatementSequencer so nested statements can reuse
///          colon-separated parsing logic.  The callback consumes the terminating
///          `END` token followed by the expected keyword, leaving the parser
///          positioned after the body.  All parsed statements are appended to
///          @p body and the location of the `END` keyword is returned for later
///          diagnostics.
///
/// @param endKind Keyword expected after END to close the body.
/// @param body Destination vector for parsed statements.
/// @return Source location of the terminating END token.
il::support::SourceLoc Parser::parseProcedureBody(TokenKind endKind, std::vector<StmtPtr> &body)
{
    auto ctx = statementSequencer();
    auto info =
        ctx.collectStatements([&](int, il::support::SourceLoc)
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
///
/// @details Invokes @ref parseProcedureBody with the `FUNCTION` terminator so
///          the helper collects statements until `END FUNCTION` is encountered.
///          The resulting end location is stored on the declaration for
///          downstream diagnostics.
///
/// @param fn Function declaration to populate with body statements.
void Parser::parseFunctionBody(FunctionDecl *fn)
{
    fn->endLoc = parseProcedureBody(TokenKind::KeywordFunction, fn->body);
}


} // namespace il::frontends::basic
