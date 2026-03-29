# Zia Missing Feature Implementation Plans

7 features documented in the Zia reference that are not yet implemented.
Discovered during the systematic language audit on 2026-03-29.

## Implementation Order

```
Phase 1 — Quick wins (no dependencies):
  04-single-expr-function.md   [S]  Parser only
  07-is-inheritance.md         [S]  Lowerer only

Phase 2 — Operator additions:
  01-shift-operators.md        [S]  Full pipeline (token → lowerer)
  02-compound-bitwise-assign.md [S] Depends on 01

Phase 3 — Expression syntax:
  03-func-lambda-syntax.md     [S]  Parser only

Phase 4 — Larger features:
  06-type-aliases.md           [M]  Full pipeline (token → sema)
  05-variadic-params.md        [M]  Full pipeline (token → lowerer)
```

## Summary

| # | Feature | Complexity | Key Files |
|---|---------|-----------|-----------|
| 01 | Shift operators `<<`/`>>` | S | Token.hpp, Lexer.cpp, Parser_Expr.cpp, Sema_Expr_Ops.cpp, Lowerer_Expr_Binary.cpp |
| 02 | Compound `<<=` `>>=` `&=` `\|=` `^=` | S | Token.hpp, Lexer.cpp, Parser_Expr.cpp |
| 03 | `func(...)` lambda syntax | S | Parser_Expr_Primary.cpp |
| 04 | Single-expression `= expr;` | S | Parser_Decl.cpp (2 locations) |
| 05 | Variadic params `...Type` | M | Token.hpp, Lexer.cpp, AST_Decl.hpp, Parser_Decl.cpp, Sema_Decl.cpp, Sema_Expr_Call.cpp, Lowerer_Expr_Call.cpp |
| 06 | Type aliases `type Name = Type` | M | Token.hpp, Lexer.cpp, AST_Decl.hpp, Parser_Decl.cpp, Sema_Decl.cpp, Sema_TypeResolution.cpp |
| 07 | `is` inheritance check | S | Lowerer_Expr_Lambda.cpp (single function) |

Total: 5 Small + 2 Medium features.
