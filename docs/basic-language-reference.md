# BASIC v0.1 Language Reference

BASIC front-end targets IL v0.1.1 and the VM interpreter.
See [IL v0.1 spec](./il-spec.md) for the underlying intermediate language.

## Goals & scope

- Deterministic subset suitable for early IDE/compiler bring-up.
- VM-first design: code lowers to IL and executes via the VM (native codegen pending).
- Covers variables, arithmetic, strings, conditionals, loops, and simple I/O.

## Program structure & line numbers

A program is a sequence of statements separated by newlines or `:` on the same line.
Line numbers are optional; when present they act as labels (`GOTO` targets).
Execution starts at the first line or statement.
Comments begin with `'` and continue to the end of the line.

```basic
10 PRINT "HELLO"
20 LET X = 2 + 3
30 IF X > 4 THEN PRINT X ELSE PRINT 4
40 END
```

## Types

| Type    | IL type | Literal examples                    | Notes |
|---------|---------|-------------------------------------|-------|
| Integer | `i64`   | `0`, `-12`, `42`                    | default numeric type |
| Float   | `f64`   | produced via `VAL` (optional)       | optional in v0.1 |
| String  | `str`   | `"text"`, escape `\" \\ \n \t \xNN` | UTF-8 |
| Boolean | `i1`    | `TRUE`, `FALSE`                     | results of comparisons |

Coercions:
- integer operations wrap on overflow.
- runtime built-ins handle string ↔ numeric conversions.

## Expressions

### Operators & precedence (high → low)

1. `()`
2. Unary `NOT`, `+`, `-`
3. `*`, `/`
4. `+`, `-`
5. Comparisons `= <> < <= > >=`
6. `AND`
7. `OR`

Arithmetic is integer based (`/` is integer division; division by zero traps).
Comparisons are allowed between like types (strings only support `=`/`<>`).
Logical operators use short-circuit evaluation and return Boolean.

### Built-in functions

| Function          | Signature               | Notes |
|-------------------|-------------------------|-------|
| `LEN(s$)`         | `str → i64`             | length in bytes |
| `MID$(s$, i, l)`  | `str × i64 × i64 → str` | 1‑based `i`; length clamped |
| `VAL(s$)`         | `str → i64`             | traps on invalid |

## Statements

| Statement | Meaning |
|-----------|---------|
| `LET v = expr` | assign to variable `v` (auto‑declare) |
| `PRINT expr` | write value to stdout |
| `IF c THEN … [ELSEIF …]* [ELSE …]` | conditional execution |
| `WHILE c … WEND` | loop while condition `c` is true |
| `FOR v = start TO end [STEP s] … NEXT v` | counted loop |
| `GOTO lineNumber` | jump to line label |
| `END` | terminate program |
| `INPUT v$` / `INPUT v` | read line as string or integer |
| `DIM A(n)` | allocate integer array of length `n` |

Multi-statement `THEN`/`ELSE` blocks can be placed on subsequent lines or separated by `:`.

## Variables & naming conventions

Names match `[A-Za-z][A-Za-z0-9_]*` with an optional `$` suffix for strings (`NAME$`).
Without `$` the variable is inferred as integer unless assigned from `VALF$` (future).
All variables are local to `@main`.
Arrays declared with `DIM` hold `i64` elements; indices are 0‑based.

## Errors & diagnostics

Compile-time errors occur on type mismatches or malformed syntax.
Runtime traps include division by zero, invalid `VAL`, and out-of-bounds `MID$`.
Diagnostics use codes prefixed with `B` and show the source line with a caret.

```text
10 LET X = 1 +
            ^
B0001: expected expression
```

## Grammar

```bnf
program     ::= (line | stmt)* EOF
line        ::= (NUMBER)? stmt (":" stmt)* NEWLINE
stmt        ::= "LET" ident "=" expr
             | "PRINT" expr
             | "DIM" ident "(" expr ")"
             | "IF" expr "THEN" stmt ("ELSEIF" expr "THEN" stmt)* ("ELSE" stmt)?
             | "WHILE" expr (NEWLINE|":") stmt* "WEND"
             | "FOR" ident "=" expr "TO" expr ("STEP" expr)? (NEWLINE|":") stmt* "NEXT" ident
             | "GOTO" NUMBER
             | "END"
             | "INPUT" ident
expr        ::= term (("+"|"-") term)*
term        ::= factor (("*"|"/") factor)*
factor      ::= NUMBER | STRING | ident | ident "(" expr ")"
             | "(" expr ")" | ("+"|"-") factor | "NOT" factor
ident       ::= NAME | NAME "$"
```

## IL mapping

The front end lowers BASIC into IL; see [IL v0.1 spec](./il-spec.md) for instruction semantics.

| BASIC snippet       | IL pattern                                                | Runtime |
|---------------------|-----------------------------------------------------------|---------|
| `PRINT "X"`         | `%s = const_str @.L; call @rt_print_str(%s)`              | `rt_print_str(str)` |
| `PRINT X`           | `%v = load i64, %slotX; call @rt_print_i64(%v)`           | `rt_print_i64(i64)` |
| `LET X = A + B`     | `load A; load B; %c = add %a,%b; store X,%c`              | — |
| `IF C THEN … ELSE …`| `%p = …cmp…; cbr %p, then, else`                          | — |
| `WHILE C … WEND`    | `br loop_head; cbr cond, loop_body, done`                 | — |
| `LEN(S$)`           | `call @rt_len(%s)`                                        | `rt_len(str)→i64` |
| `MID$(S$,i,l)`      | `call @rt_substr(%s, i-1, l)`                              | `rt_substr(str,i64,i64)→str` |
| `VAL(S$)`           | `call @rt_to_int(%s)`                                     | `rt_to_int(str)→i64` |
| `INPUT A$`          | `%s = call @rt_input_line(); store A$, %s`                | `rt_input_line()→str` |
| `INPUT N`           | `%s = call @rt_input_line(); %n = call @rt_to_int(%s); store N,%n` | `rt_input_line()→str; rt_to_int(str)→i64` |
| `DIM A(N)`          | `%bytes = mul N,8; %p = call @rt_alloc(%bytes); store %A,%p` | `rt_alloc(i64)→ptr` |
| `A(I)`              | `%base = load ptr,%A; %off = shl I,3; %ptr = gep %base,%off; %v = load i64,%ptr` | — |
| `LET A(I) = X`      | compute `%ptr` as above; `store i64,%ptr,X`               | — |

BASIC’s 1-based indices are lowered to 0-based for runtime calls.

## Examples

See the [BASIC examples](./examples/basic/) and their IL counterparts in `docs/examples/il/`.
