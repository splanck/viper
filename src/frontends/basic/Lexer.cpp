#include "frontends/basic/Lexer.h"
#include <cctype>
#include <cassert>
#include <string>

namespace il::basic {

Lexer::Lexer(std::string source, uint32_t file_id)
    : source_(std::move(source)), file_id_(file_id) {}

char Lexer::peek() const {
  if (pos_ >= source_.size()) return '\0';
  return source_[pos_];
}

char Lexer::peekNext() const {
  if (pos_ + 1 >= source_.size()) return '\0';
  return source_[pos_ + 1];
}

char Lexer::advance() {
  char c = peek();
  if (c == '\n') {
    line_++;
    column_ = 1;
  } else {
    column_++;
  }
  pos_++;
  return c;
}

bool Lexer::match(char c) {
  if (peek() != c) return false;
  advance();
  return true;
}

Token Lexer::makeToken(TokenKind kind, std::string text, int int_value) {
  Token t;
  t.kind = kind;
  t.text = std::move(text);
  t.int_value = int_value;
  t.loc = {file_id_, line_, column_};
  return t;
}

void Lexer::skipWhitespace() {
  while (true) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r') {
      advance();
    } else {
      break;
    }
  }
}

Token Lexer::lexNumber() {
  uint32_t start_col = column_;
  int value = 0;
  while (std::isdigit(peek())) {
    value = value * 10 + (advance() - '0');
  }
  Token t;
  t.kind = TokenKind::Integer;
  t.int_value = value;
  t.loc = {file_id_, line_, start_col};
  return t;
}

Token Lexer::lexString() {
  uint32_t start_col = column_;
  advance(); // consume opening quote
  std::string s;
  while (peek() && peek() != '"') {
    s.push_back(advance());
  }
  if (peek() == '"') advance();
  Token t;
  t.kind = TokenKind::String;
  t.text = s;
  t.loc = {file_id_, line_, start_col};
  return t;
}

static bool isIdentStart(char c) { return std::isalpha(c) || c == '_'; }
static bool isIdentChar(char c) {
  return std::isalnum(c) || c == '_' || c == '$';
}

Token Lexer::lexIdentifier() {
  uint32_t start_col = column_;
  std::string s;
  while (isIdentChar(peek())) {
    s.push_back(std::toupper(advance()));
  }
  Token t;
  t.loc = {file_id_, line_, start_col};
  t.text = s;
  if (s == "PRINT") t.kind = TokenKind::KeywordPrint;
  else if (s == "LET") t.kind = TokenKind::KeywordLet;
  else if (s == "IF") t.kind = TokenKind::KeywordIf;
  else if (s == "THEN") t.kind = TokenKind::KeywordThen;
  else if (s == "ELSE") t.kind = TokenKind::KeywordElse;
  else if (s == "WHILE") t.kind = TokenKind::KeywordWhile;
  else if (s == "WEND") t.kind = TokenKind::KeywordWend;
  else if (s == "GOTO") t.kind = TokenKind::KeywordGoto;
  else if (s == "END") t.kind = TokenKind::KeywordEnd;
  else t.kind = TokenKind::Identifier;
  return t;
}

std::vector<Token> Lexer::lex() {
  std::vector<Token> toks;
  while (true) {
    skipWhitespace();
    uint32_t start_col = column_;
    char c = peek();
    if (c == '\0') {
      Token t; t.kind = TokenKind::Eof; t.loc = {file_id_, line_, column_};
      toks.push_back(t); break;
    }
    if (c == '\n') {
      advance();
      Token t; t.kind = TokenKind::Newline; t.loc = {file_id_, line_-1, start_col};
      toks.push_back(t); continue;
    }
    if (std::isdigit(c)) { toks.push_back(lexNumber()); continue; }
    if (c == '"') { toks.push_back(lexString()); continue; }
    if (isIdentStart(c)) { toks.push_back(lexIdentifier()); continue; }
    advance();
    Token t; t.loc = {file_id_, line_, start_col};
    switch (c) {
    case '+': t.kind = TokenKind::Plus; break;
    case '-': t.kind = TokenKind::Minus; break;
    case '*': t.kind = TokenKind::Star; break;
    case '/': t.kind = TokenKind::Slash; break;
    case '(': t.kind = TokenKind::LParen; break;
    case ')': t.kind = TokenKind::RParen; break;
    case ',': t.kind = TokenKind::Comma; break;
    case '=': t.kind = TokenKind::Equals; break;
    case '<':
      if (match('>')) { t.kind = TokenKind::NotEqual; t.loc.column = start_col; }
      else if (match('=')) { t.kind = TokenKind::LessEqual; t.loc.column = start_col; }
      else { t.kind = TokenKind::Less; }
      break;
    case '>':
      if (match('=')) { t.kind = TokenKind::GreaterEqual; t.loc.column = start_col; }
      else { t.kind = TokenKind::Greater; }
      break;
    default: t.kind = TokenKind::Eof; break; // unknown char -> eof to terminate
    }
    toks.push_back(t);
  }
  return toks;
}

} // namespace il::basic
