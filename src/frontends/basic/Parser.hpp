// File: src/frontends/basic/Parser.hpp
// Purpose: Declares BASIC parser that builds an AST.
// Key invariants: Maintains token lookahead buffer.
// Ownership/Lifetime: Parser owns lexer and token buffer.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lexer.hpp"
#include <memory>
#include <string_view>
#include <vector>

namespace il::frontends::basic
{

class Parser
{
  public:
    Parser(std::string_view src, uint32_t file_id, DiagnosticEmitter *emitter = nullptr);
    std::unique_ptr<Program> parseProgram();

  private:
    mutable Lexer lexer_;
    mutable std::vector<Token> tokens_;
    DiagnosticEmitter *emitter_ = nullptr;

#include "frontends/basic/Parser_Token.hpp"

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
    StmtPtr parseRandomize();
    StmtPtr parseFunction();
    StmtPtr parseSub();
    StmtPtr parseReturn();
    std::vector<Param> parseParamList();
    Type typeFromSuffix(std::string_view name);

    ExprPtr parseExpression(int min_prec = 0);
    ExprPtr parsePrimary();
    int precedence(TokenKind k);
};

} // namespace il::frontends::basic
