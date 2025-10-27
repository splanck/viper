//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/parse/StmtRegistry.cpp
// Purpose: Provide the registry-backed dispatch helpers for BASIC statements.
// Key invariants: Handlers are invoked only when a keyword entry exists and may
//                 populate the AST builder with a parsed statement.
// Ownership/Lifetime: Registry stores copyable function objects without owning
//                     parser resources; TokenStream/ASTBuilder/Diagnostics hold
//                     references to the parser and never outlive it.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/parse/StmtRegistry.hpp"

#include "frontends/basic/Parser.hpp"

#include <cstdio>
#include <string>

namespace il::frontends::basic::parse
{

TokenStream::TokenStream(Parser &parser) : parser_(parser) {}

const Token &TokenStream::peek(int offset) const
{
    return parser_.peek(offset);
}

bool TokenStream::at(TokenKind kind, int offset) const
{
    return parser_.peek(offset).kind == kind;
}

Token TokenStream::consume()
{
    return parser_.consume();
}

Token TokenStream::expect(TokenKind kind)
{
    return parser_.expect(kind);
}

void TokenStream::syncToStmtBoundary()
{
    parser_.syncToStmtBoundary();
}

ASTBuilder::ASTBuilder(Parser &parser) : parser_(parser) {}

void ASTBuilder::setCurrentLine(int line)
{
    currentLine_ = line;
}

int ASTBuilder::currentLine() const
{
    return currentLine_;
}

void ASTBuilder::setStatement(StmtPtr stmt)
{
    stmt_ = std::move(stmt);
}

bool ASTBuilder::hasStatement() const
{
    return static_cast<bool>(stmt_);
}

StmtPtr ASTBuilder::takeStatement()
{
    return std::move(stmt_);
}

StmtPtr ASTBuilder::call(StmtPtr (Parser::*method)())
{
    return (parser_.*method)();
}

StmtPtr ASTBuilder::call(StmtPtr (Parser::*method)(int))
{
    return (parser_.*method)(currentLine_);
}

Diagnostics::Diagnostics(Parser &parser) : parser_(parser) {}

void Diagnostics::unexpectedLineNumber(const Token &tok)
{
    if (parser_.emitter_)
    {
        parser_.emitter_->emit(il::support::Severity::Error,
                               "B0001",
                               tok.loc,
                               static_cast<uint32_t>(tok.lexeme.size()),
                               "unexpected line number");
        return;
    }
    std::fprintf(stderr, "unexpected line number '%s'\n", tok.lexeme.c_str());
}

void Diagnostics::unknownStatement(const Token &tok, std::string_view lexeme)
{
    if (parser_.emitter_)
    {
        std::string message = std::string("unknown statement '") + std::string(lexeme) + "'";
        parser_.emitter_->emit(il::support::Severity::Error,
                               "B0001",
                               tok.loc,
                               static_cast<uint32_t>(lexeme.size()),
                               std::move(message));
        return;
    }
    std::fprintf(stderr, "unknown statement '%s'\n", tok.lexeme.c_str());
}

void Diagnostics::expectedProcedureCallParen(const Token &ident, const Token &next)
{
    il::support::SourceLoc diagLoc = next.loc.hasLine() ? next.loc : ident.loc;
    if (parser_.emitter_)
    {
        std::string message = "expected '(' after procedure name '" + ident.lexeme + "'";
        parser_.emitter_->emit(il::support::Severity::Error,
                               "B0001",
                               diagLoc,
                               1,
                               std::move(message));
        return;
    }
    std::fprintf(stderr, "expected '(' after procedure name '%s'\n", ident.lexeme.c_str());
}

void StmtRegistry::registerHandler(TokenKind kind, Handler handler)
{
    handlers_[static_cast<std::size_t>(kind)] = std::move(handler);
}

bool StmtRegistry::tryParse(TokenStream &stream, ASTBuilder &builder, Diagnostics &diags) const
{
    const Token &tok = stream.peek();
    const auto index = static_cast<std::size_t>(tok.kind);
    if (index >= handlers_.size())
        return false;
    const Handler &handler = handlers_[index];
    if (!handler)
        return false;
    return handler(stream, builder, diags);
}

bool StmtRegistry::contains(TokenKind kind) const
{
    const auto index = static_cast<std::size_t>(kind);
    return index < handlers_.size() && static_cast<bool>(handlers_[index]);
}

} // namespace il::frontends::basic::parse


