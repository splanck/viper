// File: src/frontends/basic/Parser.cpp
// Purpose: Implements program-level orchestration for the BASIC parser,
//          delegating statement parsing to Parser_Stmt.cpp.
// Key invariants: Relies on token buffer for lookahead.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// License: MIT (see LICENSE).
// Links: docs/class-catalog.md

#include "frontends/basic/Parser.hpp"
#include <cstdlib>

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

    auto setHandler = [this](TokenKind kind, StmtHandler handler) {
        stmtHandlers_[static_cast<std::size_t>(kind)] = handler;
    };
    setHandler(TokenKind::KeywordPrint, {&Parser::parsePrint, nullptr});
    setHandler(TokenKind::KeywordLet, {&Parser::parseLet, nullptr});
    setHandler(TokenKind::KeywordIf, {nullptr, &Parser::parseIf});
    setHandler(TokenKind::KeywordWhile, {&Parser::parseWhile, nullptr});
    setHandler(TokenKind::KeywordFor, {&Parser::parseFor, nullptr});
    setHandler(TokenKind::KeywordNext, {&Parser::parseNext, nullptr});
    setHandler(TokenKind::KeywordGoto, {&Parser::parseGoto, nullptr});
    setHandler(TokenKind::KeywordEnd, {&Parser::parseEnd, nullptr});
    setHandler(TokenKind::KeywordInput, {&Parser::parseInput, nullptr});
    setHandler(TokenKind::KeywordDim, {&Parser::parseDim, nullptr});
    setHandler(TokenKind::KeywordRandomize, {&Parser::parseRandomize, nullptr});
    setHandler(TokenKind::KeywordFunction, {&Parser::parseFunction, nullptr});
    setHandler(TokenKind::KeywordSub, {&Parser::parseSub, nullptr});
    setHandler(TokenKind::KeywordReturn, {&Parser::parseReturn, nullptr});
}

/// @brief Parse the entire BASIC program.
/// @return Root program node with separated procedure and main sections.
/// @note Assumes all procedures appear before the first main statement.
std::unique_ptr<Program> Parser::parseProgram()
{
    auto prog = std::make_unique<Program>();
    prog->loc = peek().loc;
    bool inMain = false;
    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();
        if (at(TokenKind::EndOfFile))
            break;
        int line = 0;
        if (at(TokenKind::Number))
        {
            line = std::atoi(peek().lexeme.c_str());
            consume();
        }
        std::vector<StmtPtr> stmts;
        while (true)
        {
            auto stmt = parseStatement(line);
            stmt->line = line;
            stmts.push_back(std::move(stmt));
            if (at(TokenKind::Colon))
            {
                consume();
                continue;
            }
            break;
        }
        StmtPtr root;
        if (stmts.size() == 1)
        {
            root = std::move(stmts.front());
        }
        else
        {
            il::support::SourceLoc loc = stmts.front()->loc;
            auto list = std::make_unique<StmtList>();
            list->line = line;
            list->loc = loc;
            list->stmts = std::move(stmts);
            root = std::move(list);
        }
        if (!inMain &&
            (dynamic_cast<FunctionDecl *>(root.get()) || dynamic_cast<SubDecl *>(root.get())))
        {
            prog->procs.push_back(std::move(root));
        }
        else
        {
            inMain = true;
            prog->main.push_back(std::move(root));
        }
        if (at(TokenKind::EndOfLine))
            consume();
    }
    return prog;
}

} // namespace il::frontends::basic
