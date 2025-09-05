# BASIC Semantics

The semantic analyzer walks the BASIC AST to perform checks and annotate
structures for later phases.

## ProcTable

A `ProcTable` maps each declared FUNCTION or SUB to its signature: kind,
return type inferred from the name suffix, and parameter types along with array
flags. This table enables call checking in later passes.

Example diagnostics emitted by the analyzer:

```
10 FUNCTION F(A, A)
              ^
error[B1005]: duplicate parameter 'A'

30 SUB S(X#())
        ^
error[B2004]: array parameter must be i64[] or str[]

50 FUNCTION S()
            ^
error[B1004]: duplicate procedure 'S'
```

## Scope stack

Each `FUNCTION` or `SUB` is analyzed with a stack of lexical scopes. A new
scope is pushed when entering a nested block and popped on exit. `DIM` adds a
symbol to the current scope; the name may shadow one from an outer scope but
redeclaring within the same scope is rejected.

## Return-path analysis

Functions must return a value along every control-flow path. The analyzer
implements a simple structural check:

- `RETURN` with an expression marks a returning path.
- `IF`/`ELSEIF`/`ELSE` returns only when all arms return.
- `WHILE` and `FOR` are assumed to possibly skip execution or loop forever and
  therefore do not guarantee a return.
- For a sequence of statements, only the final statement is considered.

If analysis fails, `missing return in FUNCTION <name>` is reported at the
`END FUNCTION` keyword. This pass is conservative and does not attempt deep
flow analysis (e.g., constant conditions or loop bounds).
