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
#include <array>
#include <cctype>
#include <cstdio>
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

int Parser::allocateSyntheticLabelNumber()
{
    while (usedLabelNumbers_.count(nextSyntheticLabel_) != 0)
        ++nextSyntheticLabel_;
    return nextSyntheticLabel_++;
}

int Parser::ensureLabelNumber(const std::string &name)
{
    auto it = namedLabels_.find(name);
    if (it != namedLabels_.end())
        return it->second.number;

    int labelNumber = allocateSyntheticLabelNumber();
    NamedLabelEntry entry{};
    entry.number = labelNumber;
    namedLabels_.emplace(name, entry);
    usedLabelNumbers_.insert(labelNumber);
    return labelNumber;
}

bool Parser::hasLabelName(const std::string &name) const
{
    return namedLabels_.find(name) != namedLabels_.end();
}

std::optional<int> Parser::lookupLabelNumber(const std::string &name) const
{
    auto it = namedLabels_.find(name);
    if (it == namedLabels_.end())
        return std::nullopt;
    return it->second.number;
}

void Parser::noteNamedLabelDefinition(const Token &tok, int labelNumber)
{
    usedLabelNumbers_.insert(labelNumber);
    auto it = namedLabels_.find(tok.lexeme);
    if (it == namedLabels_.end())
    {
        NamedLabelEntry entry{};
        entry.number = labelNumber;
        entry.defined = true;
        entry.definitionLoc = tok.loc;
        namedLabels_.emplace(tok.lexeme, entry);
        return;
    }

    if (!it->second.defined)
    {
        it->second.defined = true;
        it->second.definitionLoc = tok.loc;
        return;
    }

    if (emitter_)
    {
        std::string msg = "label '" + tok.lexeme + "' already defined";
        emitter_->emit(il::support::Severity::Error,
                       "B0001",
                       tok.loc,
                       static_cast<uint32_t>(tok.lexeme.size()),
                       std::move(msg));
    }
    else
    {
        std::fprintf(stderr, "label '%s' already defined\n", tok.lexeme.c_str());
    }
}

void Parser::noteNamedLabelReference(const Token &tok, int labelNumber)
{
    usedLabelNumbers_.insert(labelNumber);
    auto it = namedLabels_.find(tok.lexeme);
    if (it == namedLabels_.end())
    {
        NamedLabelEntry entry{};
        entry.number = labelNumber;
        entry.referenced = true;
        entry.referenceLoc = tok.loc;
        namedLabels_.emplace(tok.lexeme, entry);
        return;
    }

    auto &entry = it->second;
    entry.referenced = true;
    if (!entry.referenceLoc.isValid())
        entry.referenceLoc = tok.loc;
}

void Parser::noteNumericLabelUsage(int labelNumber)
{
    usedLabelNumbers_.insert(labelNumber);
}

/// @brief Populate the registry of statement parsing callbacks.
/// @details Populates the registry with handlers grouped by category (core,
///          control-flow, runtime, and I/O).  The registry is returned by value
///          so callers can store it in static storage without exposing
///          construction details.
Parser::StatementParserRegistry Parser::buildStatementRegistry()
{
    StatementParserRegistry registry;
    registerCoreParsers(registry);
    registerControlFlowParsers(registry);
    registerRuntimeParsers(registry);
    registerIoParsers(registry);
    registerOopParsers(registry);
    return registry;
}

/// @brief Parse an optional BASIC type annotation for function return values.
///
/// @details Consumes recognised BASIC type names used in FUNCTION headers after an
///          `AS` clause.  The helper mirrors the accepted spellings from DIM so
///          semantic analysis observes consistent defaults.  When no known type is
///          present the current token remains untouched and callers see
///          BasicType::Unknown, allowing them to diagnose the omission if
///          necessary.
///
/// @return Parsed BASIC return type or @ref BasicType::Unknown when unrecognised.
BasicType Parser::parseBasicType()
{
    const auto toUpper = [](std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (char c : text)
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        return result;
    };

    const Token &tok = peek();
    if (tok.kind != TokenKind::Identifier)
        return BasicType::Unknown;

    std::string upper = toUpper(tok.lexeme);
    if (upper == "INTEGER" || upper == "LONG" || upper == "INT")
    {
        consume();
        return BasicType::Int;
    }
    if (upper == "DOUBLE" || upper == "FLOAT")
    {
        consume();
        return BasicType::Float;
    }
    if (upper == "STRING")
    {
        consume();
        return BasicType::String;
    }
    return BasicType::Unknown;
}

/// @brief Access the singleton statement parser registry.
/// @details Initialises the registry on first use by delegating to
///          @ref buildStatementRegistry.  Subsequent calls reuse the static
///          instance, ensuring parser construction remains inexpensive.
const Parser::StatementParserRegistry &Parser::statementRegistry()
{
    static StatementParserRegistry registry = buildStatementRegistry();
    return registry;
}

/// @brief Install a statement handler that does not expect an explicit line number.
/// @details Stores the callback in the registry entry associated with @p kind.
///          The registry distinguishes between handlers that consume prefixed
///          line numbers and those that do not.
void Parser::StatementParserRegistry::registerHandler(TokenKind kind, NoArgHandler handler)
{
    entries_[static_cast<std::size_t>(kind)].first = handler;
}

/// @brief Install a statement handler that receives the parsed line number.
/// @details Complements the no-argument registration to support statements that
///          require awareness of their source line during parsing.
void Parser::StatementParserRegistry::registerHandler(TokenKind kind, WithLineHandler handler)
{
    entries_[static_cast<std::size_t>(kind)].second = handler;
}

std::pair<Parser::StatementParserRegistry::NoArgHandler,
          Parser::StatementParserRegistry::WithLineHandler>
/// @brief Retrieve registered handlers for the given token kind.
/// @details Returns the pair of callbacks for @p kind, substituting
///          `{nullptr, nullptr}` when the registry lacks an entry.  The helper
///          performs bounds checking to keep invalid token kinds from indexing
///          past the table.
Parser::StatementParserRegistry::lookup(TokenKind kind) const
{
    const auto index = static_cast<std::size_t>(kind);
    if (index >= entries_.size())
        return {nullptr, nullptr};
    return entries_[index];
}

/// @brief Test whether any handler exists for the token kind.
/// @details Uses @ref lookup to fetch the callback pair and reports true when at
///          least one handler is registered.
bool Parser::StatementParserRegistry::contains(TokenKind kind) const
{
    const auto [noArg, withLine] = lookup(kind);
    return noArg != nullptr || withLine != nullptr;
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
    bool hasUndefinedNamedLabel = false;
    for (const auto &[name, entry] : namedLabels_)
    {
        if (!entry.referenced || entry.defined)
            continue;
        hasUndefinedNamedLabel = true;
        if (emitter_)
        {
            std::string msg = "Undefined label: " + name;
            emitter_->emit(il::support::Severity::Error,
                           "B0002",
                           entry.referenceLoc,
                           static_cast<uint32_t>(name.size()),
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr, "Undefined label: %s\n", name.c_str());
        }
    }
    if (hasUndefinedNamedLabel)
        return nullptr;
    return prog;
}

} // namespace il::frontends::basic
