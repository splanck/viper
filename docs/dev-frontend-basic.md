#BASIC Frontend Developer Notes

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

The constant folder evaluates pure expressions composed only of literals. When
both operands are numeric, they are promoted so that if either is a float, the
other is converted to float before the operation. Integer arithmetic uses
64-bit wrap-around semantics. String operands fold `+` as concatenation and
support equality/inequality comparisons. Mixed string and numeric operations
are left to the semantic analyzer for diagnostics.

## AST printing conventions

`AstPrinter` renders a compact, Lisp-style representation of the AST for
debugging. An internal `Printer` helper centralizes indentation so nested
blocks are indented uniformly.

## Lowering conventions

`Lowerer` translates BASIC AST nodes to IL using small helpers per control
construct (`lowerIf`, `lowerWhile`, `lowerFor`, and `lowerPrint`). Each helper
creates or looks up blocks through the lowering context, sets the builder's
insert point, emits the condition and body, and finally branches to a merge
block. Runtime externs required by a program are declared once up front via
`declareRequiredRuntime`, keeping control-flow lowering focused on IL structure.
