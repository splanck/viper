#include "frontends/basic/Token.h"

namespace il::frontends::basic {

const char *tokenKindToString(TokenKind k) {
  switch (k) {
  case TokenKind::EndOfFile:
    return "eof";
  case TokenKind::EndOfLine:
    return "eol";
  case TokenKind::Number:
    return "number";
  case TokenKind::String:
    return "string";
  case TokenKind::Identifier:
    return "ident";
  case TokenKind::KeywordPrint:
    return "PRINT";
  case TokenKind::KeywordLet:
    return "LET";
  case TokenKind::KeywordIf:
    return "IF";
  case TokenKind::KeywordThen:
    return "THEN";
  case TokenKind::KeywordElse:
    return "ELSE";
  case TokenKind::KeywordWhile:
    return "WHILE";
  case TokenKind::KeywordWend:
    return "WEND";
  case TokenKind::KeywordGoto:
    return "GOTO";
  case TokenKind::KeywordEnd:
    return "END";
  case TokenKind::Plus:
    return "+";
  case TokenKind::Minus:
    return "-";
  case TokenKind::Star:
    return "*";
  case TokenKind::Slash:
    return "/";
  case TokenKind::Equal:
    return "=";
  case TokenKind::NotEqual:
    return "<>";
  case TokenKind::Less:
    return "<";
  case TokenKind::LessEqual:
    return "<=";
  case TokenKind::Greater:
    return ">";
  case TokenKind::GreaterEqual:
    return ">=";
  case TokenKind::LParen:
    return "(";
  case TokenKind::RParen:
    return ")";
  case TokenKind::Comma:
    return ",";
  }
  return "?";
}

} // namespace il::frontends::basic
