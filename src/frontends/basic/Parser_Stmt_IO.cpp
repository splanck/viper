//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements BASIC parser helpers for IO-related statements. The routines live
// in a dedicated translation unit so the main parser remains focused on general
// statement handling while these helpers concentrate on separator and channel
// peculiarities.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Parsing utilities for BASIC IO statements (PRINT, INPUT, OPEN, etc.).
/// @details The functions consume tokens from @ref Parser, build the appropriate
///          AST nodes, and report diagnostics through the active diagnostic
///          emitter when malformed statements are encountered.

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diag_expected.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

struct IoStmtParserState
{
    Parser &parser;
    DiagnosticEmitter *emitter = nullptr;

    IoStmtParserState(Parser &p, DiagnosticEmitter *e) : parser(p), emitter(e) {}

    const Token &peek(int n = 0) const
    {
        return parser.peek(n);
    }

    bool at(TokenKind kind) const
    {
        return parser.at(kind);
    }

    Token consume()
    {
        return parser.consume();
    }

    ExprPtr parseExpression()
    {
        return parser.parseExpression();
    }

    ExprPtr parseArrayOrVar()
    {
        return parser.parseArrayOrVar();
    }

    bool isStatementStart(TokenKind kind) const
    {
        return parser.isStatementStart(kind);
    }

    void syncToStmtBoundary()
    {
        parser.syncToStmtBoundary();
    }
};

namespace
{
using ParserState = IoStmtParserState;

[[nodiscard]] bool match(ParserState &state, TokenKind kind)
{
    if (!state.at(kind))
        return false;
    state.consume();
    return true;
}

[[nodiscard]] il::support::Expected<Token> expect(ParserState &state,
                                                  TokenKind kind,
                                                  std::string_view message = {})
{
    if (state.at(kind))
        return state.consume();

    const Token &unexpected = state.peek();
    std::string diagMessage =
        message.empty() ? std::string("expected ") + tokenKindToString(kind) : std::string(message);
    il::support::Diag errorDiag = il::support::makeError(unexpected.loc, diagMessage);

    if (state.emitter)
    {
        if (message.empty())
        {
            state.emitter->emitExpected(unexpected.kind, kind, unexpected.loc);
        }
        else
        {
            uint32_t length =
                unexpected.lexeme.empty() ? 1U : static_cast<uint32_t>(unexpected.lexeme.size());
            auto loc = errorDiag.loc.hasLine() ? errorDiag.loc : unexpected.loc;
            state.emitter->emit(il::support::Severity::Error, "B0001", loc, length, diagMessage);
        }
    }
    else
    {
        std::fprintf(stderr, "%s\n", diagMessage.c_str());
    }

    state.syncToStmtBoundary();
    return il::support::Expected<Token>(std::move(errorDiag));
}

[[nodiscard]] bool atLineEnd(ParserState &state)
{
    TokenKind kind = state.peek().kind;
    return kind == TokenKind::EndOfLine || kind == TokenKind::EndOfFile || kind == TokenKind::Colon;
}

void emitBasicError(ParserState &state, const Token &tok, std::string_view message)
{
    if (state.emitter)
    {
        uint32_t length = tok.lexeme.empty() ? 1U : static_cast<uint32_t>(tok.lexeme.size());
        state.emitter->emit(
            il::support::Severity::Error, "B0001", tok.loc, length, std::string(message));
    }
    else
    {
        std::fprintf(stderr, "%s\n", std::string(message).c_str());
    }
}

StmtPtr parsePrintStmt(ParserState &state)
{
    il::support::SourceLoc loc = state.peek().loc;
    state.consume(); // PRINT

    if (match(state, TokenKind::Hash))
    {
        auto stmt = std::make_unique<PrintChStmt>();
        stmt->loc = loc;
        stmt->mode = PrintChStmt::Mode::Print;
        stmt->channelExpr = state.parseExpression();
        stmt->trailingNewline = true;

        if (!match(state, TokenKind::Comma))
            return stmt;

        while (!atLineEnd(state))
        {
            if (state.isStatementStart(state.peek().kind))
                break;
            stmt->args.push_back(state.parseExpression());
            if (!match(state, TokenKind::Comma))
                break;
        }
        return stmt;
    }

    auto stmt = std::make_unique<PrintStmt>();
    stmt->loc = loc;

    while (!atLineEnd(state))
    {
        TokenKind kind = state.peek().kind;
        if (state.isStatementStart(kind))
            break;
        if (match(state, TokenKind::Comma))
        {
            stmt->items.push_back(PrintItem{PrintItem::Kind::Comma, nullptr});
            continue;
        }
        if (match(state, TokenKind::Semicolon))
        {
            stmt->items.push_back(PrintItem{PrintItem::Kind::Semicolon, nullptr});
            continue;
        }
        stmt->items.push_back(PrintItem{PrintItem::Kind::Expr, state.parseExpression()});
    }

    return stmt;
}

StmtPtr parseOpenStmt(ParserState &state)
{
    il::support::SourceLoc loc = state.peek().loc;
    state.consume(); // OPEN

    auto stmt = std::make_unique<OpenStmt>();
    stmt->loc = loc;
    stmt->pathExpr = state.parseExpression();

    auto forTok = expect(state, TokenKind::KeywordFor, "expected FOR in OPEN statement");
    if (!forTok)
        return nullptr;

    if (match(state, TokenKind::KeywordInput))
    {
        stmt->mode = OpenStmt::Mode::Input;
    }
    else if (match(state, TokenKind::KeywordOutput))
    {
        stmt->mode = OpenStmt::Mode::Output;
    }
    else if (match(state, TokenKind::KeywordAppend))
    {
        stmt->mode = OpenStmt::Mode::Append;
    }
    else if (match(state, TokenKind::KeywordBinary))
    {
        stmt->mode = OpenStmt::Mode::Binary;
    }
    else if (match(state, TokenKind::KeywordRandom))
    {
        stmt->mode = OpenStmt::Mode::Random;
    }
    else
    {
        const Token &unexpected = state.peek();
        emitBasicError(state, unexpected, "expected mode keyword after FOR");
        state.syncToStmtBoundary();
        return nullptr;
    }

    auto asTok = expect(state, TokenKind::KeywordAs, "expected AS in OPEN statement");
    if (!asTok)
        return nullptr;

    auto hashTok = expect(state, TokenKind::Hash, "expected '#' after AS");
    if (!hashTok)
        return nullptr;

    stmt->channelExpr = state.parseExpression();
    return stmt;
}

StmtPtr parseCloseStmt(ParserState &state)
{
    il::support::SourceLoc loc = state.peek().loc;
    state.consume(); // CLOSE

    auto stmt = std::make_unique<CloseStmt>();
    stmt->loc = loc;

    auto hashTok = expect(state, TokenKind::Hash, "expected '#' after CLOSE");
    if (!hashTok)
        return nullptr;

    stmt->channelExpr = state.parseExpression();
    return stmt;
}

StmtPtr parseInputStmt(ParserState &state)
{
    il::support::SourceLoc loc = state.peek().loc;
    state.consume(); // INPUT

    if (match(state, TokenKind::Hash))
    {
        auto channelTok = expect(state, TokenKind::Number, "expected channel number after '#'");
        if (!channelTok)
            return nullptr;

        auto commaTok = expect(state, TokenKind::Comma, "expected ',' after channel number");
        if (!commaTok)
            return nullptr;

        auto targetTok = expect(state, TokenKind::Identifier, "expected variable name for INPUT #");
        if (!targetTok)
            return nullptr;

        auto stmt = std::make_unique<InputChStmt>();
        stmt->loc = loc;
        stmt->channel = std::atoi(channelTok.value().lexeme.c_str());
        stmt->target.name = targetTok.value().lexeme;
        stmt->target.loc = targetTok.value().loc;

        if (match(state, TokenKind::Comma))
        {
            const Token &extra = state.peek();
            emitBasicError(state, extra, "INPUT # with multiple targets not yet supported");
            state.syncToStmtBoundary();
        }

        return stmt;
    }

    ExprPtr prompt;
    if (state.at(TokenKind::String))
    {
        const Token &promptTok = state.peek();
        auto str = std::make_unique<StringExpr>();
        str->loc = promptTok.loc;
        str->value = promptTok.lexeme;
        prompt = std::move(str);
        state.consume();

        auto commaTok = expect(state, TokenKind::Comma, "expected ',' after INPUT prompt");
        if (!commaTok)
            return nullptr;
    }

    auto stmt = std::make_unique<InputStmt>();
    stmt->loc = loc;
    stmt->prompt = std::move(prompt);

    auto nameTok = expect(state, TokenKind::Identifier, "expected variable name in INPUT");
    if (!nameTok)
        return nullptr;
    stmt->vars.push_back(nameTok.value().lexeme);

    while (match(state, TokenKind::Comma))
    {
        auto nextTok = expect(state, TokenKind::Identifier, "expected variable name in INPUT");
        if (!nextTok)
            return nullptr;
        stmt->vars.push_back(nextTok.value().lexeme);
    }

    return stmt;
}

} // namespace

/// @brief Register parsing functions for IO-related statement keywords.
/// @details Populates the provided registry so the generic parser dispatch can
///          map BASIC keywords (PRINT, OPEN, etc.) to their specialised handler
///          methods on @ref Parser.
/// @param registry Dispatcher that maps tokens to member function pointers.
void Parser::registerIoParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordPrint, &Parser::parsePrintStatement);
    registry.registerHandler(TokenKind::KeywordWrite, &Parser::parseWriteStatement);
    registry.registerHandler(TokenKind::KeywordOpen, &Parser::parseOpenStatement);
    registry.registerHandler(TokenKind::KeywordClose, &Parser::parseCloseStatement);
    registry.registerHandler(TokenKind::KeywordSeek, &Parser::parseSeekStatement);
    registry.registerHandler(TokenKind::KeywordInput, &Parser::parseInputStatement);
    registry.registerHandler(TokenKind::KeywordLine, &Parser::parseLineInputStatement);
}

/// @brief Parse the PRINT statement, supporting both console and channel forms.
/// @details Delegates to an internal helper that flattens control flow for the
///          various PRINT forms.
/// @return Newly allocated AST node representing the parsed statement.
StmtPtr Parser::parsePrintStatement()
{
    ParserState state{*this, emitter_};
    return parsePrintStmt(state);
}

/// @brief Parse the WRITE# statement.
/// @details Handles channel-prefixed WRITE statements, requiring a hash marker
///          and comma-separated expression list. Unlike PRINT#, WRITE# does not
///          permit null items, so expressions are parsed greedily until no more
///          commas remain.
/// @return AST node representing the WRITE# statement.
StmtPtr Parser::parseWriteStatement()
{
    auto loc = peek().loc;
    consume(); // WRITE
    expect(TokenKind::Hash);
    auto stmt = std::make_unique<PrintChStmt>();
    stmt->loc = loc;
    stmt->mode = PrintChStmt::Mode::Write;
    stmt->trailingNewline = true;
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    while (true)
    {
        stmt->args.push_back(parseExpression());
        if (!at(TokenKind::Comma))
            break;
        consume();
    }
    return stmt;
}

/// @brief Parse the OPEN statement configuring file channels.
/// @details Delegates to a helper that validates modes and required markers.
/// @return AST node describing the OPEN statement.
StmtPtr Parser::parseOpenStatement()
{
    ParserState state{*this, emitter_};
    return parseOpenStmt(state);
}

/// @brief Parse the CLOSE statement.
/// @details Delegates to a helper that validates the channel marker.
/// @return AST node describing the CLOSE statement.
StmtPtr Parser::parseCloseStatement()
{
    ParserState state{*this, emitter_};
    return parseCloseStmt(state);
}

StmtPtr Parser::parseSeekStatement()
{
    auto loc = peek().loc;
    consume(); // SEEK
    auto stmt = std::make_unique<SeekStmt>();
    stmt->loc = loc;
    expect(TokenKind::Hash);
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    stmt->positionExpr = parseExpression();
    return stmt;
}

/// @brief Parse the INPUT statement, supporting prompt and variable lists.
/// @details Delegates to a helper that handles prompt detection, channel forms,
///          and diagnostics in a linear fashion.
/// @return AST node for the parsed INPUT statement.
StmtPtr Parser::parseInputStatement()
{
    ParserState state{*this, emitter_};
    return parseInputStmt(state);
}

/// @brief Parse the `LINE INPUT` statement that reads an entire line.
/// @details Supports the channel-prefixed form (`LINE INPUT #`) and validates
///          that the destination is a simple variable or array element. When an
///          invalid target is provided, diagnostics are emitted and a fallback
///          placeholder variable is inserted so compilation can proceed.
/// @return AST node describing the LINE INPUT statement.
StmtPtr Parser::parseLineInputStatement()
{
    auto loc = peek().loc;
    consume(); // LINE
    expect(TokenKind::KeywordInput);
    expect(TokenKind::Hash);
    auto stmt = std::make_unique<LineInputChStmt>();
    stmt->loc = loc;
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    auto target = parseArrayOrVar();
    Expr *rawTarget = target.get();
    if (rawTarget && !dynamic_cast<VarExpr *>(rawTarget) && !dynamic_cast<ArrayExpr *>(rawTarget))
    {
        il::support::SourceLoc diagLoc = rawTarget->loc.hasLine() ? rawTarget->loc : loc;
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error, "B0001", diagLoc, 1, "expected variable");
        }
        auto fallback = std::make_unique<VarExpr>();
        fallback->loc = diagLoc;
        stmt->targetVar = std::move(fallback);
    }
    else
    {
        stmt->targetVar = std::move(target);
    }
    return stmt;
}

} // namespace il::frontends::basic
