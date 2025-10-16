//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the façade for the BASIC parser.  This translation unit initialises
// the token buffer, wires statement keywords to their parsing callbacks, and
// exposes the program-level entry point that produces the AST.  Detailed
// statement parsing is delegated to Parser_Stmt.cpp; expression parsing lives in
// Parser_Expr.cpp.
//
//===----------------------------------------------------------------------===//

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
}

/// @brief Create a statement sequencer bound to this parser instance.
///
/// The sequencer coordinates optional line numbers and separator trivia while
/// delegating actual statement parsing back to the parser.  Returning a value by
/// value keeps the API ergonomic and allows callers to compose sequencing steps
/// without leaking parser internals.
///
/// @return Sequencer that shares access to the parser's token stream.
StatementSequencer Parser::statementSequencer()
{
    return StatementSequencer(*this);
}

Parser::StatementParserRegistry Parser::buildStatementRegistry()
{
    StatementParserRegistry registry;
    registerCoreParsers(registry);
    registerControlFlowParsers(registry);
    registerRuntimeParsers(registry);
    registerIoParsers(registry);
    return registry;
}

const Parser::StatementParserRegistry &Parser::statementRegistry()
{
    static StatementParserRegistry registry = buildStatementRegistry();
    return registry;
}

void Parser::StatementParserRegistry::registerHandler(TokenKind kind,
                                                      NoArgHandler handler)
{
    entries_[static_cast<std::size_t>(kind)].first = handler;
}

void Parser::StatementParserRegistry::registerHandler(TokenKind kind,
                                                      WithLineHandler handler)
{
    entries_[static_cast<std::size_t>(kind)].second = handler;
}

std::pair<Parser::StatementParserRegistry::NoArgHandler,
          Parser::StatementParserRegistry::WithLineHandler>
Parser::StatementParserRegistry::lookup(TokenKind kind) const
{
    const auto index = static_cast<std::size_t>(kind);
    if (index >= entries_.size())
        return {nullptr, nullptr};
    return entries_[index];
}

bool Parser::StatementParserRegistry::contains(TokenKind kind) const
{
    const auto [noArg, withLine] = lookup(kind);
    return noArg != nullptr || withLine != nullptr;
}

/// @brief Parse the entire BASIC program.
/// @return Root program node with separated procedure and main sections.
/// @note Collects procedure declarations regardless of their position.
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
        if (dynamic_cast<FunctionDecl *>(root.get()) || dynamic_cast<SubDecl *>(root.get()))
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
