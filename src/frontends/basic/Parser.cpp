//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Parser.cpp
// Purpose: Implement the façade that orchestrates BASIC parsing and routes work
//          to specialised statement and expression modules.
// Links: docs/basic-language.md#parser
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Houses high-level orchestration logic for the BASIC parser.
/// @details Initialises the token buffer, constructs statement parser
///          registries, and exposes the top-level parse routine that produces
///          the AST.  Specific statement and expression production rules are
///          delegated to sibling translation units.

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/parse/StmtRegistry.hpp"
#include <cstdlib>
#include <utility>

namespace il::frontends::basic
{
/// @brief Construct a parser for the given source.
/// @details Instantiates the lexer, stores the diagnostic emitter pointer, and
///          seeds the token buffer with the first lookahead token.  The eager
///          priming simplifies the rest of the parser, which can assume at least
///          one token is buffered before processing productions.
/// @param src Full BASIC source to parse.
/// @param file_id Identifier for diagnostics.
/// @param emitter Destination for emitted diagnostics.
Parser::Parser(std::string_view src, uint32_t file_id, DiagnosticEmitter *emitter)
    : lexer_(src, file_id), emitter_(emitter)
{
    tokens_.push_back(lexer_.next());
}

/// @brief Create a statement sequencer bound to this parser instance.
/// @details Returns a lightweight helper that coordinates optional line numbers
///          and statement separators before delegating to parser callbacks.
///          Exposing the sequencer by value keeps the API ergonomic while still
///          encapsulating token-buffer access.
/// @return Sequencer that shares access to the parser's token stream.
StatementSequencer Parser::statementSequencer()
{
    return StatementSequencer(*this);
}

/// @brief Populate the registry of statement parsing callbacks.
/// @details Populates the registry with handlers grouped by category (core,
///          control-flow, runtime, and I/O).  The registry is returned by value
///          so callers can store it in static storage without exposing
///          construction details.
parse::StmtRegistry Parser::buildStatementRegistry()
{
    parse::StmtRegistry registry;
    registerCoreParsers(registry);
    registerControlFlowParsers(registry);
    registerRuntimeParsers(registry);
    registerIoParsers(registry);
    registerOopParsers(registry);
    return registry;
}

/// @brief Access the singleton statement parser registry.
/// @details Initialises the registry on first use by delegating to
///          @ref buildStatementRegistry.  Subsequent calls reuse the static
///          instance, ensuring parser construction remains inexpensive.
const parse::StmtRegistry &Parser::statementRegistry()
{
    static parse::StmtRegistry registry = buildStatementRegistry();
    return registry;
}

/// @brief Parse the entire BASIC program.
/// @details Iteratively consumes statement lines using the sequencer, routing
///          procedure declarations to @ref Program::procs while collecting other
///          statements into @ref Program::main.  Parsing continues until an
///          end-of-file token is encountered; empty lines are skipped.  The
///          program node inherits its source location from the first token to
///          aid diagnostics.
/// @return Root program node with separated procedure and main sections.
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
