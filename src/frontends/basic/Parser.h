#pragma once
#include "frontends/basic/AST.h"
#include "frontends/basic/Lexer.h"
#include <memory>
#include <string_view>

namespace il::frontends::basic {

class Parser {
public:
  Parser(std::string_view src, uint32_t file_id);
  std::unique_ptr<Program> parseProgram();

private:
  Token current_;
  Lexer lexer_;

  void advance();
  bool check(TokenKind k) const { return current_.kind == k; }
  bool consume(TokenKind k);

  StmtPtr parseStatement(int line);
  StmtPtr parsePrint();
  StmtPtr parseLet();
  StmtPtr parseIf(int line);
  StmtPtr parseWhile();
  StmtPtr parseGoto();
  StmtPtr parseEnd();

  ExprPtr parseExpression(int min_prec = 0);
  ExprPtr parsePrimary();
  int precedence(TokenKind k);
};

} // namespace il::frontends::basic
