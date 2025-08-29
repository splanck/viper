#pragma once
#include <string>
#include <vector>
#include "frontends/basic/Token.h"

namespace il::basic {

class Lexer {
public:
  Lexer(std::string source, uint32_t file_id);
  std::vector<Token> lex();

private:
  char peek() const;
  char peekNext() const;
  char advance();
  bool match(char c);
  void skipWhitespace();
  Token makeToken(TokenKind kind, std::string text = "", int int_value = 0);
  Token lexNumber();
  Token lexString();
  Token lexIdentifier();

  std::string source_;
  size_t pos_ = 0;
  uint32_t line_ = 1;
  uint32_t column_ = 1;
  uint32_t file_id_;
};

} // namespace il::basic
