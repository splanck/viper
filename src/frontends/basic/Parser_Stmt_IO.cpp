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

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "support/diag_expected.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

namespace il::frontends::basic
{
namespace
{
using il::support::Expected;

struct ParserState
{
    DiagnosticEmitter *emitter = nullptr;
    std::function<const Token &(int)> peek;
    std::function<Token()> consume;
    std::function<void()> syncToStmtBoundary;
    std::function<ExprPtr()> parseExpression;
    std::function<bool(TokenKind)> isStatementStart;
};

[[nodiscard]] uint32_t caretLength(const Token &tok)
{
    return tok.lexeme.empty() ? 1U : static_cast<uint32_t>(tok.lexeme.size());
}

template <class MessageFn>
Expected<Token> expect(ParserState &state, TokenKind expectedKind, MessageFn &&buildMessage)
{
    if (state.peek(0).kind == expectedKind)
        return state.consume();

    const Token unexpected = state.peek(0);
    std::string message = buildMessage(unexpected);
    if (state.emitter)
    {
        state.emitter->emit(il::support::Severity::Error,
                            "B0001",
                            unexpected.loc,
                            caretLength(unexpected),
                            message);
    }
    else
    {
        std::fprintf(stderr, "%s\n", message.c_str());
    }
    state.syncToStmtBoundary();
    return Expected<Token>{il::support::makeError(unexpected.loc, std::move(message))};
}

[[nodiscard]] bool match(ParserState &state, TokenKind kind)
{
    if (state.peek(0).kind != kind)
        return false;
    state.consume();
    return true;
}

[[nodiscard]] bool atLineEnd(ParserState &state)
{
    const TokenKind kind = state.peek(0).kind;
    return kind == TokenKind::EndOfFile || kind == TokenKind::EndOfLine || kind == TokenKind::Colon;
}

void consumeToLineEnd(ParserState &state)
{
    while (!atLineEnd(state))
        state.consume();
}

StmtPtr parsePrintStmt(ParserState &state);
StmtPtr parseInputStmt(ParserState &state);
StmtPtr parseOpenStmt(ParserState &state);
StmtPtr parseCloseStmt(ParserState &state);

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

namespace
{

StmtPtr parsePrintStmt(ParserState &state)
{
    const auto loc = state.peek(0).loc;
    state.consume(); // PRINT

    if (match(state, TokenKind::Hash))
    {
        auto stmt = std::make_unique<PrintChStmt>();
        stmt->loc = loc;
        stmt->mode = PrintChStmt::Mode::Print;
        stmt->trailingNewline = true;
        stmt->channelExpr = state.parseExpression();

        if (!match(state, TokenKind::Comma))
            return stmt;

        while (!atLineEnd(state))
        {
            if (state.isStatementStart && state.isStatementStart(state.peek(0).kind))
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
        const TokenKind kind = state.peek(0).kind;
        if (state.isStatementStart && state.isStatementStart(kind))
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

StmtPtr parseInputChannelStmt(ParserState &state, il::support::SourceLoc loc)
{
    auto channelTok = expect(state,
                             TokenKind::Number,
                             [](const Token &got) {
                                 return std::string("expected channel number after '#'") +
                                        ", got " + tokenKindToString(got.kind);
                             });
    if (!channelTok)
        return nullptr;

    auto commaTok = expect(state,
                           TokenKind::Comma,
                           [](const Token &got) {
                               return std::string("expected ',' after channel number") +
                                      ", got " + tokenKindToString(got.kind);
                           });
    if (!commaTok)
        return nullptr;

    auto targetTok = expect(state,
                            TokenKind::Identifier,
                            [](const Token &got) {
                                return std::string("expected variable name after INPUT #") +
                                       ", got " + tokenKindToString(got.kind);
                            });
    if (!targetTok)
        return nullptr;

    auto stmt = std::make_unique<InputChStmt>();
    stmt->loc = loc;
    const Token &channelToken = channelTok.value();
    const Token &targetToken = targetTok.value();
    stmt->channel = std::atoi(channelToken.lexeme.c_str());
    stmt->target.name = targetToken.lexeme;
    stmt->target.loc = targetToken.loc;

    if (!match(state, TokenKind::Comma))
        return stmt;

    if (state.emitter)
    {
        const Token &extra = state.peek(0);
        state.emitter->emit(il::support::Severity::Error,
                            "B0001",
                            extra.loc,
                            caretLength(extra),
                            "INPUT # with multiple targets not yet supported");
    }
    else
    {
        std::fprintf(stderr, "INPUT # with multiple targets not yet supported\n");
    }

    consumeToLineEnd(state);
    return stmt;
}

StmtPtr parseInputStmt(ParserState &state)
{
    const auto loc = state.peek(0).loc;
    state.consume(); // INPUT

    if (match(state, TokenKind::Hash))
        return parseInputChannelStmt(state, loc);

    ExprPtr prompt;
    if (state.peek(0).kind == TokenKind::String)
    {
        auto promptExpr = std::make_unique<StringExpr>();
        promptExpr->loc = state.peek(0).loc;
        promptExpr->value = state.peek(0).lexeme;
        prompt = std::move(promptExpr);
        state.consume();

        auto commaTok = expect(state,
                               TokenKind::Comma,
                               [](const Token &got) {
                                   return std::string("expected ',' after INPUT prompt") +
                                          ", got " + tokenKindToString(got.kind);
                               });
        if (!commaTok)
            return nullptr;
    }

    auto stmt = std::make_unique<InputStmt>();
    stmt->loc = loc;
    stmt->prompt = std::move(prompt);

    auto firstVar = expect(state,
                           TokenKind::Identifier,
                           [](const Token &got) {
                               return std::string("expected variable name in INPUT statement") +
                                      ", got " + tokenKindToString(got.kind);
                           });
    if (!firstVar)
        return nullptr;
    stmt->vars.push_back(firstVar.value().lexeme);

    while (match(state, TokenKind::Comma))
    {
        auto nextVar = expect(state,
                              TokenKind::Identifier,
                              [](const Token &got) {
                                  return std::string("expected variable name after ',' in INPUT") +
                                         ", got " + tokenKindToString(got.kind);
                              });
        if (!nextVar)
            return nullptr;
        stmt->vars.push_back(nextVar.value().lexeme);
    }

    return stmt;
}

StmtPtr parseOpenStmt(ParserState &state)
{
    const auto loc = state.peek(0).loc;
    state.consume(); // OPEN

    auto stmt = std::make_unique<OpenStmt>();
    stmt->loc = loc;
    stmt->pathExpr = state.parseExpression();

    auto forTok = expect(state,
                         TokenKind::KeywordFor,
                         [](const Token &got) {
                             return std::string("expected FOR keyword in OPEN statement") +
                                    ", got " + tokenKindToString(got.kind);
                         });
    if (!forTok)
        return nullptr;

    const TokenKind modeKind = state.peek(0).kind;
    switch (modeKind)
    {
        case TokenKind::KeywordInput:
            state.consume();
            stmt->mode = OpenStmt::Mode::Input;
            break;
        case TokenKind::KeywordOutput:
            state.consume();
            stmt->mode = OpenStmt::Mode::Output;
            break;
        case TokenKind::KeywordAppend:
            state.consume();
            stmt->mode = OpenStmt::Mode::Append;
            break;
        case TokenKind::KeywordBinary:
            state.consume();
            stmt->mode = OpenStmt::Mode::Binary;
            break;
        case TokenKind::KeywordRandom:
            state.consume();
            stmt->mode = OpenStmt::Mode::Random;
            break;
        default:
        {
            const Token unexpected = state.peek(0);
            if (state.emitter)
            {
                state.emitter->emit(il::support::Severity::Error,
                                     "B0001",
                                     unexpected.loc,
                                     caretLength(unexpected),
                                     "expected INPUT, OUTPUT, APPEND, BINARY, or RANDOM");
            }
            else
            {
                std::fprintf(stderr,
                             "expected OPEN mode keyword, got %s\n",
                             tokenKindToString(unexpected.kind));
            }
            state.syncToStmtBoundary();
            return nullptr;
        }
    }

    auto asTok = expect(state,
                        TokenKind::KeywordAs,
                        [](const Token &got) {
                            return std::string("expected AS keyword in OPEN statement") +
                                   ", got " + tokenKindToString(got.kind);
                        });
    if (!asTok)
        return nullptr;

    auto hashTok = expect(state,
                          TokenKind::Hash,
                          [](const Token &got) {
                              return std::string("expected '#'") +
                                     ", got " + tokenKindToString(got.kind);
                          });
    if (!hashTok)
        return nullptr;

    stmt->channelExpr = state.parseExpression();
    return stmt;
}

StmtPtr parseCloseStmt(ParserState &state)
{
    const auto loc = state.peek(0).loc;
    state.consume(); // CLOSE

    auto stmt = std::make_unique<CloseStmt>();
    stmt->loc = loc;

    auto hashTok = expect(state,
                          TokenKind::Hash,
                          [](const Token &got) {
                              return std::string("expected '#'") +
                                     ", got " + tokenKindToString(got.kind);
                          });
    if (!hashTok)
        return nullptr;

    stmt->channelExpr = state.parseExpression();
    return stmt;
}

} // namespace

StmtPtr Parser::parsePrintStatement()
{
    ParserState state{};
    state.emitter = emitter_;
    state.peek = [this](int n) -> const Token & { return this->peek(n); };
    state.consume = [this]() { return this->consume(); };
    state.syncToStmtBoundary = [this]() { this->syncToStmtBoundary(); };
    state.parseExpression = [this]() { return this->parseExpression(); };
    state.isStatementStart = [this](TokenKind kind) { return this->isStatementStart(kind); };
    return parsePrintStmt(state);
}

StmtPtr Parser::parseInputStatement()
{
    ParserState state{};
    state.emitter = emitter_;
    state.peek = [this](int n) -> const Token & { return this->peek(n); };
    state.consume = [this]() { return this->consume(); };
    state.syncToStmtBoundary = [this]() { this->syncToStmtBoundary(); };
    state.parseExpression = [this]() { return this->parseExpression(); };
    state.isStatementStart = [this](TokenKind kind) { return this->isStatementStart(kind); };
    return parseInputStmt(state);
}

StmtPtr Parser::parseOpenStatement()
{
    ParserState state{};
    state.emitter = emitter_;
    state.peek = [this](int n) -> const Token & { return this->peek(n); };
    state.consume = [this]() { return this->consume(); };
    state.syncToStmtBoundary = [this]() { this->syncToStmtBoundary(); };
    state.parseExpression = [this]() { return this->parseExpression(); };
    state.isStatementStart = [this](TokenKind kind) { return this->isStatementStart(kind); };
    return parseOpenStmt(state);
}

StmtPtr Parser::parseCloseStatement()
{
    ParserState state{};
    state.emitter = emitter_;
    state.peek = [this](int n) -> const Token & { return this->peek(n); };
    state.consume = [this]() { return this->consume(); };
    state.syncToStmtBoundary = [this]() { this->syncToStmtBoundary(); };
    state.parseExpression = [this]() { return this->parseExpression(); };
    state.isStatementStart = [this](TokenKind kind) { return this->isStatementStart(kind); };
    return parseCloseStmt(state);
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


/// @brief Parse the SEEK statement.
/// @details Expects `SEEK #` followed by the channel expression and a comma
///          separating the position expression. Both operands are parsed as
///          general expressions.
/// @return AST node describing the SEEK statement.
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
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           diagLoc,
                           1,
                           "expected variable");
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

