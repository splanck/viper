#pragma once
#include "support/source_manager.h"
#include <string>

namespace il::frontends::basic {

enum class TokenKind {
  EndOfFile,
  EndOfLine,
  Number,
  String,
  Identifier,
  KeywordPrint,
  KeywordLet,
  KeywordIf,
  KeywordThen,
  KeywordElse,
  KeywordWhile,
  KeywordWend,
  KeywordGoto,
  KeywordEnd,
  Plus,
  Minus,
  Star,
  Slash,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  LParen,
  RParen,
  Comma,
};

struct Token {
  TokenKind kind;
  std::string lexeme;
  il::support::SourceLoc loc;
};

const char *tokenKindToString(TokenKind k);

} // namespace il::frontends::basic
