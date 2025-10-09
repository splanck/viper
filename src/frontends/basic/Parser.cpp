// File: src/frontends/basic/Parser.cpp
// Purpose: Implements program-level orchestration for the BASIC parser,
//          delegating statement parsing to Parser_Stmt.cpp.
// Key invariants: Relies on token buffer for lookahead.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// License: MIT (see LICENSE).
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include <array>
#include <cstdlib>
#include <utility>

namespace il::frontends::basic
{
/// @brief Construct a parser for the given source.
/// @param src Full BASIC source to parse.
/// @param file_id Identifier for diagnostics.
/// @param emitter Destination for emitted diagnostics.
/// @note Initializes the token buffer with the first token for lookahead.
Parser::Parser(std::string_view src, uint32_t file_id, DiagnosticEmitter *emitter)
    : lexer_(src, file_id), emitter_(emitter)
{
    tokens_.push_back(lexer_.next());
    static constexpr std::array<std::pair<TokenKind, StmtHandler>, 26> kStatementHandlers = {{
        {TokenKind::KeywordPrint, {&Parser::parsePrint, nullptr}},
        {TokenKind::KeywordLet, {&Parser::parseLet, nullptr}},
        {TokenKind::KeywordIf, {nullptr, &Parser::parseIf}},
        {TokenKind::KeywordWhile, {&Parser::parseWhile, nullptr}},
        {TokenKind::KeywordDo, {&Parser::parseDo, nullptr}},
        {TokenKind::KeywordFor, {&Parser::parseFor, nullptr}},
        {TokenKind::KeywordNext, {&Parser::parseNext, nullptr}},
        {TokenKind::KeywordExit, {&Parser::parseExit, nullptr}},
        {TokenKind::KeywordGoto, {&Parser::parseGoto, nullptr}},
        {TokenKind::KeywordGosub, {&Parser::parseGosub, nullptr}},
        {TokenKind::KeywordOpen, {&Parser::parseOpen, nullptr}},
        {TokenKind::KeywordClose, {&Parser::parseClose, nullptr}},
        {TokenKind::KeywordCls, {&Parser::parseCls, nullptr}},
        {TokenKind::KeywordColor, {&Parser::parseColor, nullptr}},
        {TokenKind::KeywordOn, {&Parser::parseOnErrorGoto, nullptr}},
        {TokenKind::KeywordResume, {&Parser::parseResume, nullptr}},
        {TokenKind::KeywordEnd, {&Parser::parseEnd, nullptr}},
        {TokenKind::KeywordInput, {&Parser::parseInput, nullptr}},
        {TokenKind::KeywordLine, {&Parser::parseLineInput, nullptr}},
        {TokenKind::KeywordLocate, {&Parser::parseLocate, nullptr}},
        {TokenKind::KeywordDim, {&Parser::parseDim, nullptr}},
        {TokenKind::KeywordRedim, {&Parser::parseReDim, nullptr}},
        {TokenKind::KeywordRandomize, {&Parser::parseRandomize, nullptr}},
        {TokenKind::KeywordFunction, {&Parser::parseFunction, nullptr}},
        {TokenKind::KeywordSub, {&Parser::parseSub, nullptr}},
        {TokenKind::KeywordReturn, {&Parser::parseReturn, nullptr}},
    }};
    for (const auto &[kind, handler] : kStatementHandlers)
    {
        stmtHandlers_[static_cast<std::size_t>(kind)] = handler;
    }
}

StatementSequencer Parser::statementSequencer()
{
    return StatementSequencer(*this);
}

/// @brief Parse the entire BASIC program.
/// @return Root program node with separated procedure and main sections.
/// @note Procedures are collected regardless of their position relative to
///       main statements.
std::unique_ptr<Program> Parser::parseProgram()
{
    auto prog = std::make_unique<Program>();
    prog->loc = peek().loc;
    auto seq = statementSequencer();
    while (!at(TokenKind::EndOfFile))
    {
        seq.skipLineBreaks();
        if (at(TokenKind::EndOfFile))
            break;
        auto root = seq.parseStatementLine();
        if (!root)
            continue;
        if (dynamic_cast<FunctionDecl *>(root.get()) ||
            dynamic_cast<SubDecl *>(root.get()))
        {
            prog->procs.push_back(std::move(root));
        }
        else
        {
            prog->main.push_back(std::move(root));
        }
    }
    return prog;
}

} // namespace il::frontends::basic
