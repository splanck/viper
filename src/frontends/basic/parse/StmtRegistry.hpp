//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/parse/StmtRegistry.hpp
// Purpose: Declare the statement dispatch helpers used by the BASIC parser.
// Key invariants: Registry maintains deterministic keyword-to-handler mapping.
// Ownership/Lifetime: Registry stores function objects with parser references
//                     resolved at parse time; no additional resources owned.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/Token.hpp"
#include "frontends/basic/ast/NodeFwd.hpp"

#include <array>
#include <functional>
#include <string_view>

namespace il::frontends::basic
{
class Parser;

namespace parse
{
class TokenStream
{
  public:
    explicit TokenStream(Parser &parser);

    const Token &peek(int offset = 0) const;
    bool at(TokenKind kind, int offset = 0) const;
    Token consume();
    Token expect(TokenKind kind);
    void syncToStmtBoundary();

  private:
    Parser &parser_;
};

class ASTBuilder
{
  public:
    explicit ASTBuilder(Parser &parser);

    void setCurrentLine(int line);
    [[nodiscard]] int currentLine() const;

    void setStatement(StmtPtr stmt);
    [[nodiscard]] bool hasStatement() const;
    StmtPtr takeStatement();

    StmtPtr call(StmtPtr (Parser::*method)());
    StmtPtr call(StmtPtr (Parser::*method)(int));

  private:
    Parser &parser_;
    int currentLine_ = 0;
    StmtPtr stmt_;
};

class Diagnostics
{
  public:
    explicit Diagnostics(Parser &parser);

    void unexpectedLineNumber(const Token &tok);
    void unknownStatement(const Token &tok, std::string_view lexeme);
    void expectedProcedureCallParen(const Token &ident, const Token &next);

  private:
    Parser &parser_;
};

class StmtRegistry
{
  public:
    using Handler = std::function<bool(TokenStream &, ASTBuilder &, Diagnostics &)>;

    void registerHandler(TokenKind kind, Handler handler);
    bool tryParse(TokenStream &stream, ASTBuilder &builder, Diagnostics &diags) const;
    bool contains(TokenKind kind) const;

  private:
    std::array<Handler, static_cast<std::size_t>(TokenKind::Count)> handlers_{};
};

} // namespace parse
} // namespace il::frontends::basic

