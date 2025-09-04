# BASIC Frontend Developer Notes

## Parser structure

The BASIC parser is split into focused components:

- `Parser.cpp` handles program and statement parsing.
- `Parser_Expr.cpp` implements expression parsing using a Pratt parser.
- `Parser_Token.hpp` / `Parser_Token.cpp` provide token helpers:
  - `bool at(TokenKind)`
  - `const Token &peek(int)`
  - `Token consume()`
  - `Token expect(TokenKind, const char*)`
  - `void syncToStmtBoundary()`

This separation keeps statement logic clear and isolates token mechanics and
expression handling.
