// File: src/frontends/basic/Parser.hpp
// Purpose: Declares BASIC parser that builds an AST.
// Key invariants: Parser state tracks current token.
// Ownership/Lifetime: Parser does not own token buffer.
// Links: docs/class-catalog.md
#pragma once
#include "frontends/basic/AST.hpp"
#include "frontends/basic/Lexer.hpp"
#include <memory>
#include <string_view>

namespace il::frontends::basic
{

class Parser
{
  public:
    Parser(std::string_view src, uint32_t file_id);
    std::unique_ptr<Program> parseProgram();

  private:
    Token current_;
    Lexer lexer_;

    void advance();
    bool check(TokenKind k) const
    {
        return current_.kind == k;
    }
    bool consume(TokenKind k);

    StmtPtr parseStatement(int line);
    StmtPtr parsePrint();
    StmtPtr parseLet();
    StmtPtr parseIf(int line);
    StmtPtr parseWhile();
    StmtPtr parseFor();
    StmtPtr parseNext();
    StmtPtr parseGoto();
    StmtPtr parseEnd();
    StmtPtr parseInput();
    StmtPtr parseDim();

    ExprPtr parseExpression(int min_prec = 0);
    ExprPtr parsePrimary();
    int precedence(TokenKind k);
};

} // namespace il::frontends::basic
