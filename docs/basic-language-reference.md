# BASIC v0.1 Language Reference

BASIC programs lower to [IL v0.1.1](./il-spec.md) and run on the VM interpreter. This document describes the subset implemented in v0.1.

## Goals & Scope

- Deterministic subset for early IDE/compiler bring-up.
- VM-first design: source lowers to IL; native codegen is future work.
- Includes variables, arithmetic, strings, conditionals, loops, and simple I/O.

## Program Structure and Line Numbers

A program is a sequence of statements separated by newlines. Line numbers are optional labels (`GOTO` targets); execution starts at the first statement. Comments begin with `'` and continue to end of line.

### Multi-statement Lines with `:`

Multiple statements on the same line separated by `:` execute left-to-right.

```basic
10 LET A = 1 : PRINT A : LET A = A + 1 : PRINT A
```

## Types

| Type    | IL type | Literal examples                     | Notes                       |
| ------- | ------- | ------------------------------------ | --------------------------- |
| Integer | `i64`   | `0`, `-12`, `42`                     | default numeric type, wraps |
| Float   | `f64`   | produced via `VAL`                   | optional in v0.1            |
| String  | `str`   | `"text"`, escape `\\" \\ \n \t \xNN` | UTF-8                       |
| Boolean | `i1`    | `TRUE`, `FALSE`                      | results of comparisons      |

## Expressions

### Operators and Precedence (high → low)

1. `()`
2. Unary `NOT`, `+`, `-`
3. `*`, `/`
4. `+`, `-`
5. Comparisons `= <> < <= > >=`
6. `AND`
7. `OR`

Arithmetic is integer-based (`/` is integer division; division by zero traps). Comparisons require like types (strings only support `=`/`<>`). Logical operators short-circuit and return Boolean.

### Comparisons

String expressions may be compared for equality or inequality only:

```basic
10 IF "HI" = "HI" THEN PRINT 1
20 IF "A" <> "B" THEN PRINT 2
```

Relational operators `<`, `>`, `<=`, `>=` on strings are illegal and produce a compile-time error:

```basic
30 IF "A" < "B" THEN PRINT 3  ' error
```

### Unary Operators

`NOT e` evaluates `e` as a Boolean, yielding `1` if `e` is zero and `0` otherwise.

```basic
10 LET X = 0
20 IF NOT X THEN PRINT 1
30 IF NOT 5 THEN PRINT 0
```

## Statements

| Statement                                | Meaning                                                                  |
| ---------------------------------------- | ------------------------------------------------------------------------ |
| `LET v = expr`                           | assign to variable `v` (auto-declare)                                   |
| `PRINT items`                            | write values to stdout; `,` inserts space, `;` suppresses newline       |
| `IF c THEN … [ELSEIF …]* [ELSE …]`       | conditional execution                                                   |
| `WHILE c … WEND`                         | loop while condition `c` is true                                        |
| `FOR v = start TO end [STEP s] … NEXT v` | counted loop                                                            |
| `GOTO line`                              | jump to line label                                                       |
| `END`                                    | terminate program                                                        |
| `INPUT v$` / `INPUT v` / `INPUT "p", v`  | read line as string or integer, optional literal prompt                 |
| `DIM A(n)`                               | allocate integer array of length `n`                                    |

An optional string literal prompt may precede the variable:

```basic
INPUT "N=", N
```

## Variables and Naming

Identifiers match `[A-Za-z][A-Za-z0-9_]*` with optional `$` suffix for strings. Without `$` the variable defaults to integer. All variables are local to `@main`. `DIM` arrays store `i64` elements with 0-based indices.

## Errors and Diagnostics

Compile-time errors report syntax or type issues. Runtime traps include division by zero, invalid `VAL`, and out-of-bounds `MID$`. Diagnostics use codes prefixed with `B` and show source line with a caret.

## Grammar (informal)

```bnf
program   ::= (line | stmt)* EOF
line      ::= NUMBER? stmt (":" stmt)*
stmt      ::= "LET" ident "=" expr
           | "PRINT" (expr | "," | ";")+
           | "DIM" ident "(" expr ")"
           | "IF" expr "THEN" stmt ("ELSEIF" expr "THEN" stmt)* ("ELSE" stmt)?
           | "WHILE" expr (NEWLINE | ":") stmt* "WEND"
           | "FOR" ident "=" expr "TO" expr ("STEP" expr)? (NEWLINE | ":") stmt* "NEXT" ident
           | "GOTO" NUMBER
           | "END"
           | "INPUT" (STRING ",")? ident
expr      ::= term (("+" | "-") term)*
term      ::= factor (("*" | "/") factor)*
factor    ::= NUMBER | STRING | ident | ident "(" expr ")" | "(" expr ")" | ("+" | "-") factor | "NOT" factor
ident     ::= NAME | NAME "$"
```

## IL Mapping

The front end lowers BASIC to IL; see the [IL v0.1.1 spec](./il-spec.md) for instruction semantics.

| BASIC snippet | IL pattern | Runtime |
| ------------- | ---------- | ------- |
| `PRINT "X"`   | `%s = const_str @.L;`<br>`call @rt_print_str(%s)` | `rt_print_str(str)` |
| `PRINT X`     | `%v = load i64, %slotX;`<br>`call @rt_print_i64(%v)` | `rt_print_i64(i64)` |
| `PRINT "A";` | `call @rt_print_str("A")` | `rt_print_str(str)` |
| `PRINT "A", 1` | `call @rt_print_str("A");`<br>`call @rt_print_str(" ");`<br>`call @rt_print_i64(1);`<br>`call @rt_print_str("\n")` | `rt_print_str(str)`<br>`rt_print_i64(i64)` |
