# BASIC v0.1 Language Reference

Status: Implemented subset for front-end bring-up
Back end: Lowers to IL v0.1.1 → VM interpreter (native codegen WIP)

## Goals & Scope

- Small, predictable BASIC subset suitable for early IDE/compiler bring-up.
- Deterministic semantics that map cleanly to IL and runtime calls.
- Feature set covering variables, arithmetic, strings, conditionals, loops, and simple I/O.

## Programs & Structure

A program is a sequence of statements separated by newlines or `:` on the same line. Line
numbers are optional; when present they act as labels (targets for `GOTO`). Execution
starts at the first line (or the first statement if no numbers). Comments start with `'`
and run to the end of the line.

```basic
10 PRINT "HELLO"
20 LET X = 2 + 3
30 IF X > 4 THEN PRINT X ELSE PRINT 4
40 END
```

## Types

- **Integer:** 64-bit signed (`i64` in IL). Literal examples: `0`, `-12`, `42`.
- **Float:** optional in v0.1; produced via `VAL` of string; 64-bit IEEE (`f64`).
- **String:** UTF-8 sequences. Literal: `"text"`, escapes `\"` `\\` `\n` `\t` `\xNN`.
- **Boolean:** expression result `TRUE`/`FALSE` (internally `i1`), accepted in conditions.

### Coercions

- Integer + Integer → Integer (wraps on overflow in IL).
- `LEN`, `VAL`, `MID$` provide string↔numeric operations via runtime.
- `PRINT` chooses `rt_print_str` for strings, `rt_print_i64` for integers,
  `rt_print_f64` for floats.

## Expressions

Precedence (high → low):

1. `()`
1. Unary: `NOT`, `+`, `-`
1. `*`, `/`
1. `+`, `-` (binary)
1. Comparisons: `= <> < <= > >=` (yield Boolean)
1. `AND`
1. `OR`

Operators:

- Arithmetic on integers; `/` is integer division (traps on div/0).
- Comparisons allowed between like types (int with int, str with str using `=`/`<>` only).
- Logical operators `AND`/`OR`/`NOT` use short-circuit evaluation and return Boolean.

Built-ins (all map to runtime; see §10):

- `LEN(s$) -> integer`
- `MID$(s$, start, length) -> string` (1-based indices; clamped)
- `VAL(s$) -> integer` (traps on invalid; `VALF$` for float optional later)

## Statements

- `LET var = expr` — assign (vars auto-declared on first use).
- `PRINT expr` — output value.
- `IF cond THEN stmt {ELSEIF cond THEN stmt}* [ELSE stmt]`.
- Multi-statement THEN/ELSE blocks: chain multiple statements on subsequent lines or use
  `:` separators.
- `WHILE cond ... WEND`.
- `FOR var = start TO end [STEP s] ... NEXT var`.
- `GOTO lineNumber`.
- `END` — terminate program.
- `INPUT var$ | INPUT var` — read a line from stdin. For `NAME$` variables the line is
  stored as-is (leading/trailing spaces kept). For integer variables the line is trimmed
  and converted via `VAL`; invalid numbers trap.

## Variables & Names

Names: `[A-Za-z][A-Za-z0-9_]*` with optional `$` suffix for strings (`NAME$`).

Type inference:

- `$` suffix → string.
- otherwise integer unless assigned from `VALF$(…)` (future).

All variables are function-local to `@main` in v0.1.

Arrays: `DIM A(N)` allocates `N` `i64` elements and must appear before `A` is used.
Indices are 0-based; only integer arrays are supported. Out-of-bounds access is undefined
(no checks yet).

## Errors

- Division by zero, invalid `VAL`, out-of-bounds `MID$` length/start after clamping →
  runtime trap with message.
- Type mismatch in comparisons/operations → compile-time (lowering) error.

## Diagnostics

Errors use standardized codes prefixed with `B`. Messages show the source line and a
caret.

```text
10 LET X = 1 +
            ^
B0001: expected expression.
```

Runtime traps use codes like `B0002` for division by zero.

## Grammar (informal)

```text
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
factor      ::= NUMBER | STRING | ident | ident "(" expr ")" | "(" expr ")" | ("+"|"-") factor | "NOT" factor
ident       ::= NAME | NAME "$"
```

## Mapping to IL & Runtime

| BASIC | IL pattern | Runtime |
| :----------- | :------------------------------------------------------ | :---------------------------------- |
| `PRINT "X"` | `%s = const_str @.L; call @rt_print_str(%s)` | `rt_print_str(str)` |
| `PRINT X` | `%v = load i64, %slotX; call @rt_print_i64(%v)` | `rt_print_i64(i64)` |
| `LET X = A + B` | `load A; load B; %c = add %a,%b; store X,%c` | — |
| `IF C THEN … ELSE …` | `%p = …cmp…; cbr %p, then, else` | — |
| `WHILE C … WEND` | `br loop_head; cbr cond, loop_body, done` | — |
| `LEN(S$)` | `call @rt_len(%s)` | `rt_len(str)->i64` |
| `MID$(S$,i,l)` | `call @rt_substr(%s, i-1, l)` | `rt_substr(str,i64,i64)->str` |
| `VAL(S$)` | `call @rt_to_int(%s)` | `rt_to_int(str)->i64` |
| `INPUT A$` | `%s = call @rt_input_line(); store A$, %s` | `rt_input_line()->str` |
| `INPUT N` | `%s = call @rt_input_line(); %n = call @rt_to_int(%s); store N, %n` | `rt_input_line()->str; rt_to_int(str)->i64` |
| `DIM A(N)` | `%bytes = mul N,8; %p = call @rt_alloc(%bytes); store ptr %p, %A` | `rt_alloc(i64)->ptr` |
| `A(I)` | `%base = load ptr, %A; %off = shl I,3; %ptr = gep %base,%off; %v = load i64,%ptr` | — |
| `LET A(I) = X` | `compute %ptr as above; store i64,%ptr,X` | — |

Indexing: BASIC’s 1-based indices are lowered to 0-based for runtime calls (subtract 1).

## Examples

See `/docs/examples/basic/` and the IL equivalents in `/docs/examples/il/`.
