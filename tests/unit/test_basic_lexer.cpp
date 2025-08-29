#include "frontends/basic/Lexer.h"
#include <cassert>
#include <string>

int main() {
  il::basic::Lexer lex("10 PRINT \"hi there\"\n", 1);
  auto toks = lex.lex();
  assert(toks[0].kind == il::basic::TokenKind::Integer && toks[0].int_value == 10);
  assert(toks[1].kind == il::basic::TokenKind::KeywordPrint);
  assert(toks[2].kind == il::basic::TokenKind::String && toks[2].text == "hi there");
  assert(toks[3].kind == il::basic::TokenKind::Newline);
  assert(toks[4].kind == il::basic::TokenKind::Eof);

  il::basic::Lexer lex2("pRiNt 1\n", 1);
  auto toks2 = lex2.lex();
  assert(toks2[0].kind == il::basic::TokenKind::KeywordPrint);
  assert(toks2[1].kind == il::basic::TokenKind::Integer);
  return 0;
}
