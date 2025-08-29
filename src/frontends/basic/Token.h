#pragma once
#include <string>
#include "support/source_manager.h"
namespace il::basic {

enum class TokenKind {
  Eof,
  Newline,
  Identifier,
  Integer,
  String,
  Plus,
  Minus,
  Star,
  Slash,
  Equals,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  NotEqual,
  LParen,
  RParen,
  Comma,
  KeywordPrint,
  KeywordLet,
  KeywordIf,
  KeywordThen,
  KeywordElse,
  KeywordWhile,
  KeywordWend,
  KeywordGoto,
  KeywordEnd,
};

struct Token {
  TokenKind kind;
  support::SourceLoc loc;
  std::string text; // for identifiers/strings
  int int_value = 0;
};

} // namespace il::basic
