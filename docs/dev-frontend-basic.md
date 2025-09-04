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

## Constant Folding Rules

The BASIC constant folder reduces pure literal expressions before semantic
analysis. Binary folding uses a dispatch table with these rules:

- Numeric operands promote to float if either side is a float.
- Integer arithmetic wraps on 64-bit overflow.
- `/` always yields a float; `\` and `MOD` require integers.
- String `+` concatenates literals; `=` and `<>` compare strings.
- Mixed string/number operations are rejected with diagnostic `B2001`.
