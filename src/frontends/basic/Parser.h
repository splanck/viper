#pragma once
#include <memory>
#include <vector>
#include "frontends/basic/AST.h"
#include "frontends/basic/Token.h"

namespace il::basic {

class Parser {
public:
  explicit Parser(std::vector<Token> tokens);
  Program parse();

private:
  const Token &peek() const;
  bool match(TokenKind kind);
  const Token &consume(TokenKind kind);

  std::unique_ptr<Stmt> parseLine();
  std::unique_ptr<Stmt> parseStmt();
  std::unique_ptr<Stmt> parseInlineStmt();
  std::unique_ptr<Expr> parseExpr(int prec = 0);
  std::unique_ptr<Expr> parsePrimary();

  int getPrecedence(TokenKind kind) const;

  std::vector<Token> tokens_;
  size_t pos_ = 0;
};

} // namespace il::basic
