#BASIC v0.1 Language Reference

BASIC programs lower to [IL v0.1.2](./il-spec.md) and run on the VM interpreter. This document describes the subset implemented in v0.1.

## Goals & scope

- Deterministic subset for early IDE/compiler bring-up.
- VM-first design: source lowers to IL;
native codegen is future work.- Includes variables, arithmetic, strings, conditionals, loops,
    simple I / O.

               ##Program structure &line numbers

                   A program is a sequence of statements separated by newlines.Line numbers are
                       optional labels(`GOTO` targets);
execution starts at the first statement. Comments begin with `'` and continue to end of line.

### Multi-statement lines with `:`

Multiple statements on the same line separated by `:` execute left-to-right.

```basic
10 LET A = 1 : PRINT A : LET A = A + 1 : PRINT A
```

prints:

```
1
2
```

```basic
10 PRINT "HELLO"
20 LET X = 2 + 3
30 IF X > 4 THEN PRINT X ELSE PRINT 4
40 END
```

## Types

| Type    | IL type | Literal examples                           | Notes                               |
| ------- | ------- | ------------------------------------------ | ----------------------------------- |
| Integer | `i64`   | `0`, `-12`, `42`                           | default numeric type, wraps         |
| Float   | `f64`   | `12.34`, `.5`, `1e3`, `2.5E-2`             | variables use `#` suffix;
int widens|
| String  | `str`   | `"text"`, escape `\\" \\ \n \t \xNN` | UTF-8                               |
| Boolean | `i1`    | `TRUE`, `FALSE`                            | results of comparisons              |

## Expressions

### Operators & precedence (high → low)

1. `()`
2. Unary `NOT`, `+`, `-`
3. `*`, `/`, `\\`, `MOD`
4. `+`, `-`
5. Comparisons `= <> < <= > >=`
6. `AND`
7. `OR`

Arithmetic is integer based; `\` performs integer division with quotient
truncated toward zero, and `MOD` computes the remainder with the same sign as
the dividend. Division by zero traps at runtime. Mixed numeric operations and
comparisons implicitly widen integers to floats.
Comparisons require like types (strings only support `=`/`<>`).
Logical operators short-circuit and return Boolean.

### Comparisons

String expressions may be compared for equality or inequality only:

```basic
10 IF "HI" = "HI" THEN PRINT 1
20 IF "A" <> "B" THEN PRINT 2
```

Relational operators `<`, `>`, `<=`, `>=` on strings are illegal and produce a
compile-time error:

```basic
30 IF "A" < "B" THEN PRINT 3  ' error
```

### Unary operators

`NOT e` evaluates `e` as a Boolean, yielding `1` if `e` is zero and `0` otherwise. It binds tighter than `*`/`/` and loosens to integers when assigned.

```basic
10 LET X = 0
20 IF NOT X THEN PRINT 1
30 IF NOT 5 THEN PRINT 0
```

### Built-in functions

| Function          | Signature                    | Notes |
| ----------------- | ---------------------------- | ----- |
| `LEN(s$)`         | `str → i64`                  | length in bytes |
| `MID$(s$, i [,n])`| `str × i64 × i64? → str`     | 1-based start; `n` optional |
| `LEFT$(s$, n)`    | `str × i64 → str`            | `MID$(s$, 1, n)` |
| `RIGHT$(s$, n)`   | `str × i64 → str`            | `MID$(s$, LEN(s$) - n + 1, n)` |
| `STR$(i)` / `STR$(x #)` | `i64 → str` / `f64 → str` | decimal formatting |
| `VAL(s$)`         | `str → i64`                  | ignores leading/trailing spaces; traps on invalid numeric |
| `INT(x #)`        | `f64 → i64`                  | truncates toward zero |
| `SQR(x)`          | `num → f64`                  | square root |
| `ABS(i)` / `ABS(x #)` | `i64 → i64` / `f64 → f64` | absolute value; mixed numeric expressions widen integers to floats before taking the absolute value |
| `FLOOR(x)`        | `num → f64`                  | round down |
| `CEIL(x)`         | `num → f64`                  | round up |
| `SIN(x)`          | `num → f64`                  | sine |
| `COS(x)`          | `num → f64`                  | cosine |
| `POW(x, y)`       | `num × num → f64`            | power |
| `RND()`           | `() → f64`                   | pseudo-random [0,1); seed with `RANDOMIZE` |

```basic
PRINT ABS(-5)
PRINT ABS(-1.5#)
PRINT ABS(1-2)      ' 1
PRINT ABS(1-2.5#)   ' 1.5
PRINT FLOOR(1.9#)
PRINT CEIL(1.1#)
PRINT SQR(9#)
PRINT SIN(0#)
PRINT COS(0#)
PRINT POW(2#,10#)
```

    Indices are 1 - based. `MID$` treats `i <= 0` as `1` and returns an empty string when
`i > LEN(s$)`.Omitting `n` extracts to the end of the string.Counts `n <=
        0` yield empty strings and values exceeding the available length are clamped.
`LEFT$`/`RIGHT$` clamp `n` to `[0, LEN(s$)]` before slicing
                    .

`VAL` ignores leading and trailing whitespace before parsing decimal text
                    .Invalid numeric text traps at runtime. `INT` truncates toward zero
    : `INT(1.9)` yields `1` and `INT(-1.9)` yields `-
            1`.

```basic PRINT STR$(VAL("42")) PRINT INT(1.9) PRINT INT(-1.9)
```

            ##Statements
    | Statement | Meaning | | -- -- -- -- -- -| -- -- -- -- -|
    | `LET v = expr` | assign to variable `v` (auto - declare) |
               | `PRINT items` | write values to stdout;

separators : `,` inserts space, `;
` inserts nothing;
newline appended unless statement ends with `;` |
| `IF c THEN … [ELSEIF …]* [ELSE …]` | conditional execution |
| `WHILE c … WEND` | loop while condition `c` is true |
| `FOR v = start TO end [STEP s] … NEXT v` | counted loop |
| `GOTO line` | jump to line label |
| `END` | terminate program |
| `INPUT v$` / `INPUT v` / `INPUT "p", v` | read line as string or integer, optional literal prompt |
| `DIM A(n)` | allocate integer array of length `n` |
| `RANDOMIZE n` | seed pseudo-random generator with integer `n` (floats truncate) |
| `FUNCTION name[(params)] ... END FUNCTION` | define function, return type from name suffix |
| `SUB name[(params)] ... END SUB` | define subroutine |
| `RETURN [expr]` | return from FUNCTION (expression required); SUB must use bare `RETURN` |

### Procedures

FUNCTION names derive their return type from a suffix: `name$` returns `str`,
`name#` returns `f64`, and names without a suffix return `i64`. SUB always
returns `void`. Parameter types similarly follow the name suffix and may be
marked as arrays with `()`. Array parameters are limited to `i64[]` and
`str[]`; `f64[]` is currently rejected. Parameter names must be unique within a
procedure.

Parameters may include a type suffix (`#` for `f64`, `$` for `str`). Appending `()` denotes a 1-D array parameter.

#### Calling FUNCTIONS and SUBS

Invoke a FUNCTION or SUB by writing `name(expr, ...)`. The number of
arguments must match the declaration. Each argument must be type compatible
with its parameter; `i64` values may widen to `f64`, but numeric and string
types are not coerced. Array parameters are passed by reference – supply the
array variable itself, not an indexed element like `A(i)`.
SUBs do not yield a value and may only appear as standalone statements; using a
SUB in an expression is an error. String parameters behave like handles and are
effectively passed by reference.

An optional string literal prompt may precede the variable:

```basic
INPUT "N=", N
```

### Random numbers

`RANDOMIZE` seeds the deterministic generator used by `RND()`.

```basic
RANDOMIZE 1
PRINT RND() : PRINT RND() : PRINT RND()
```

prints

```
0.345001
0.752709
0.795745
```

The prompt must be a literal string for now.

### PRINT separators

| Separator | Effect      |
| --------- | ----------- |
| `,`       | print space |

| `;
` | print nothing;
if last
    , suppress newline |

          An example with trailing `;

`:

```basic 10 PRINT "A";
20 PRINT "B"
```

        prints `AB` on one
            line.The semicolon after `"A"` suppresses the newline so the next
`PRINT` continues on the same line.

        Multi -
        statement `THEN`/`ELSE` blocks may appear on new lines or
    be separated by `:`.

    ## #IF /
        ELSEIF /
        ELSE

`IF` evaluates a Boolean expression and executes its `THEN` block when true.Additional tests may
            follow using `ELSEIF` (one word) or `ELSE IF` (two words); the first matching branch runs. A final `ELSE` handles the default case. `ELSEIF` is equivalent to nesting another `IF` inside the `ELSE` branch.

```basic
10 LET X = 2
20 IF X = 1 THEN PRINT "ONE" ELSEIF X = 2 THEN PRINT "TWO" ELSE PRINT "OTHER"
```

prints `TWO`.

## Variables & naming conventions

Identifiers match `[A-Za-z][A-Za-z0-9_]*` with optional `$` suffix for strings
or `#` for floats. Without a suffix the variable defaults to integer. Assigning
an integer value to a float variable widens the value; assigning a float to an
integer variable is a compile-time error. All variables are local to `@main`.
`DIM` arrays store `i64` elements with 0-based indices.

### Locals and scope

`DIM` inside a `FUNCTION` or `SUB` declares a local array. The name is visible
from its declaration to the end of the enclosing block. Inner blocks may
shadow outer locals by reusing a name; the innermost definition is used. A
duplicate `DIM` of the same name within one block is a compile-time error.

### Optional debug bounds checks

`ilc` can insert runtime bounds checks for `DIM` arrays when invoked with
`--bounds-checks`. Accessing an index less than 0 or greater than or equal to
the declared length traps with `bounds check failed:
A[i]`.The checks are omitted by default and have no effect on program layout.

        ##Errors &diagnostics Compile -
        time errors report syntax or
    type issues.Runtime traps include division by zero,
    invalid `VAL`,
    and out - of -
        bounds `MID$`.Diagnostics use codes prefixed with `B` and show source line with a caret.

```text 10 LET X = 1 + ^B0001 : expected expression

```

                                 ##Grammar

```bnf program :: =
                        (line | stmt) *EOF line :: = (NUMBER)
    ? stmt(":" stmt) *NEWLINE stmt :: =
          "LET" ident "=" expr | "PRINT"(expr | "," | ";") + | "DIM" ident "(" expr ")" |
          "IF" expr "THEN" stmt("ELSEIF" expr "THEN" stmt) * ("ELSE" stmt)
      ? | "WHILE" expr(NEWLINE | ":") stmt * "WEND" | "FOR" ident "=" expr "TO" expr("STEP" expr)
      ? (NEWLINE | ":") stmt * "NEXT" ident | "GOTO" NUMBER | "END" | "INPUT"(STRING ",")
      ? ident expr :: = term(("+" | "-") term) *term :: =
            factor(("*" | "/" | "\\" | "MOD") factor) *factor :: =
                NUMBER | STRING | ident | ident "(" expr ")" | "(" expr ")" | ("+" | "-") factor |
                "NOT" factor ident ::
                    = NAME | NAME "$"
```

                     ##IL mapping The front end lowers BASIC to IL; see the [IL v0.1.2 spec](./il-spec.md) for instruction semantics.

| BASIC snippet            | IL pattern                | Runtime             |
| ------------------------ | ------------------------- | ------------------- | ----------- | ------------------------- | ------------------------- |
| `PRINT "X"`              | `%s = const_str @.L;
| | call @rt_print_str(% s)` | `rt_print_str(str)` | | `PRINT X` | `% v = load i64, % slotX;
| | call @rt_print_i64(% v)` | `rt_print_i64(i64)` | | `PRINT "A";
| | ` | `call @rt_print_str("A")` | `rt_print_str(str)` | | `PRINT "A",
    1` | `call @rt_print_str("A");
|

    call @rt_print_str(" ");
call @rt_print_i64(1);
call @rt_print_str("\n")`|`rt_print_str(str)`, `rt_print_i64(i64)`| |`PRINT "A";
1`|`call @rt_print_str("A");
call @rt_print_i64(1);
call @rt_print_str("\n")`|`rt_print_str(str)`, `rt_print_i64(i64)`| |`LET X = A + B`|`load A;
load B;
% c = add % a, % b;
store X, % c`| — | |`IF C THEN … ELSE …`| `% p = …cmp…;
cbr % p, then, else `| — | |`WHILE C … WEND`|`br loop_head;
cbr cond, loop_body,
    done`| — | |`LEN(S$)` | `call @rt_len(% s)` | `rt_len(str)→i64` | | `MID$(S$, i, l)` | `call
        @rt_substr(% s, i - 1, l)` | `rt_substr(str, i64, i64)→str` |
        | `VAL(S$)`|`call @rt_to_int(% s)`|`rt_to_int(str)→i64`|
        |`INPUT A$` | `% s = call @rt_input_line();
store A$, % s`|`rt_input_line()→str`| |`INPUT N`|`% s = call @rt_input_line();
% n = call @rt_to_int(% s);
store N, % n`|`rt_input_line()→str;
rt_to_int(str)→i64`| |`INPUT "N=", N`|`call @rt_print_str("N=");
% s = call @rt_input_line();
% n = call @rt_to_int(% s);
store N, % n`|`rt_print_str(str);
rt_input_line()→str;
rt_to_int(str)→i64`| |`DIM A(N)`|`% bytes = mul N, 8;
% p = call @rt_alloc(% bytes);
store % A, % p`|`rt_alloc(i64)→ptr`| |`A(I)`|`% base = load ptr, % A;
% off = shl I, 3;
% ptr = gep % base, % off;
% v = load i64, % ptr`| — | |`LET A(I) = X`| compute`% ptr`as above;
`store i64, % ptr,
    X` | — |

        BASIC's 1-based indices become 0-based for runtime calls.

        ##Debugging the interpreter When a program hangs due to an infinite loop,
    run it with a step limit :

```sh ilc - run tests / data / loop.il-- max -
    steps 5
```

    This aborts after five instructions and prints

`VM : step limit exceeded(5);
aborting.`

    ##Examples See the[BASIC examples](./ examples / basic /) and
    their IL counterparts.
