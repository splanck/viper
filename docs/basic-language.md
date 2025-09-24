---
status: active
audience: public
last-verified: 2025-09-23
---

<a id="basic-reference"></a>
## Reference

### Comments
Single-line comments begin with an apostrophe `'` or the keyword `REM`. `REM` is case-insensitive and must appear at the start of a line or after whitespace. Both forms ignore the rest of the line.

```basic
' Using apostrophe
REM Using REM
10 PRINT "HI" ' trailing comment
20 REM trailing REM comment
```

### Program structure
Programs can include top-level statements and user-defined procedures. Procedures may appear before or after the main program, and both sections are optional.

```basic
10 Greet
20 PRINT Square(3)
30 END
40 SUB Greet
50   PRINT "hi"
60 END SUB
70 FUNCTION Square(N)
80   RETURN N * N
90 END FUNCTION
```

### Data types
* Integers are 64-bit by default.
* Floating-point variables use the `#` suffix and are `f64`.
* Strings use the `$` suffix.
* Arrays are one-dimensional and declared with `DIM`.

```basic
10 LET I = 42
20 LET F# = 3.14
30 LET S$ = "hi"
40 DIM A(5)
50 LET A(1) = I
```

### Arrays (1-D)
Arrays use zero-based indexing. `DIM` allocates a fixed-length array and `REDIM`
changes its length in place. `LBOUND(arrayVar)` always returns `0`, and
`UBOUND(arrayVar)` evaluates to `len - 1`. Elements are currently stored as
`Int32`; additional element types are planned.

```basic
10 DIM A(3)
20 LET A(0) = 10
30 PRINT LBOUND(A), UBOUND(A)
40 PRINT A(0), A(2)
50 PRINT A(3)  ' runtime error: index 3 is out of bounds (array length is 3)
60 REDIM A(5)
70 PRINT UBOUND(A)
```

Out-of-bounds accesses trigger a runtime bounds check that terminates the
program with an error message.

### Variables and assignment
Variables are created with `LET`. Scalars are named without suffix (int), with `#` (float), or `$` (string).

```basic
10 LET A = 1
20 LET B# = 2.5
30 LET C$ = "OK"
```

### Expressions
Arithmetic operators include `+`, `-`, `*`, `/`, integer division `\\`, and `MOD`.

```basic
10 LET X = 5 \\ 2
20 PRINT X MOD 2
```

### PRINT
`PRINT` outputs comma-separated expressions.

```basic
10 LET X = 1
20 PRINT "X=", X
```

### INPUT
`INPUT` reads a line into a variable. Prompts may appear before the semicolon.

```basic
10 INPUT "Name? "; N$
20 PRINT "Hi ", N$
```

### Control flow

#### IF / ELSEIF / ELSE / END IF
Conditional blocks evaluate boolean expressions. Each branch ends with `END IF`.

```basic
10 IF A = 1 THEN
20   PRINT "one"
30 ELSEIF A = 2 THEN
40   PRINT "two"
50 ELSE
60   PRINT "other"
70 END IF
```

#### WHILE / WEND
`WHILE` loops continue while the condition is `TRUE`.

```basic
10 WHILE I < 3
20   PRINT I
30   LET I = I + 1
40 WEND
```

#### FOR / NEXT
`FOR` loops iterate over an inclusive range.

```basic
10 FOR I = 1 TO 3
20   PRINT I
30 NEXT I
```

#### Multi-statement lines
Separate statements on the same line with `:`.

```basic
10 LET A = 1 : PRINT A
```

### Procedures
BASIC procedures come in two forms: `FUNCTION` (returns a value) and `SUB` (no return). Procedures may be declared anywhere in the file and invoked before or after their definitions.

#### FUNCTION
`FUNCTION` returns a value. The return type is derived from the name suffix (`#` → `f64`, `$` → `str`, none → `i64`). Parameters are passed by value unless noted. See [procedure definitions](lowering/basic-to-il.md#procedure-definitions) for lowering details.

```basic
10 FUNCTION Add(X, Y)
20   RETURN X + Y
30 END FUNCTION
```

#### SUB
`SUB` declares a procedure with no return value. Parameter rules match those of `FUNCTION`.

```basic
10 SUB Hello
20   PRINT "hi"
30 END SUB
```

#### Parameters
Scalar parameters are passed by value. Array parameters are passed by reference and must be supplied as bare array variables declared with `DIM`.

```basic
10 SUB S(X())
20 END SUB
30 DIM A(10), B(10)
40 CALL S(B)        ' OK
50 CALL S(A(1))     ' error: argument must be an array variable (ByRef)
60 CALL S(A + 0)    ' error: argument must be an array variable (ByRef)
```

### Boolean expressions
The literals `TRUE` and `FALSE` are case-insensitive. Boolean operators follow this precedence (higher binds tighter):

| Precedence | Operators | Notes |
|------------|-----------|-------|
| 1 | `NOT` | Unary logical negation |
| 2 | `ANDALSO`, `AND` | `ANDALSO` short-circuits; `AND` always evaluates both operands |
| 3 | `ORELSE`, `OR` | `ORELSE` short-circuits; `OR` always evaluates both operands |

Conditions in `IF`, `WHILE`, and `UNTIL` statements must be boolean expressions. Numeric values do not implicitly convert to `BOOLEAN`; use comparisons (for example `X <> 0`).

### RETURN
`RETURN` exits the current `FUNCTION` or `SUB`. In a `FUNCTION`, assign the result to the function name before returning. The lowering emits an IL `ret`; see [return statements](lowering/basic-to-il.md#return-statements).

### Arrays
Declare arrays with `DIM` and index with parentheses.

```basic
10 DIM A(2)
20 FOR I = 0 TO 2
30   LET A(I) = I
40 NEXT I
50 LET S = 0
60 FOR I = 0 TO 2
70   LET S = S + A(I)
80 NEXT I
90 PRINT S
```

### Intrinsic functions

#### Indexing and substrings
String positions are 1-based. Substring functions interpret the starting position as 1 = first character. When a length is supplied, it counts characters from the starting position.

#### LEN
Returns the length of a string.

```basic
PRINT LEN("HELLO")
```

#### LEFT$
Returns the leftmost `n` characters.

```basic
PRINT LEFT$("hello", 3)
```

#### RIGHT$
Returns the rightmost `n` characters.

```basic
PRINT RIGHT$("hello", 2)
```

#### MID$
Extracts a substring starting at position.

```basic
PRINT MID$("hello", 2, 2)
```

#### INSTR
Finds the first occurrence of `needle$` in `haystack$`.

```basic
PRINT INSTR("HELLO", "LO")
```

#### LTRIM$
Removes leading whitespace.

```basic
PRINT LTRIM$("  hi")
```

#### RTRIM$
Removes trailing whitespace.

```basic
PRINT RTRIM$("hi  ")
```

#### TRIM$
Removes leading and trailing whitespace.

```basic
PRINT TRIM$("  hi  ")
```

#### UCASE$
Converts to upper case (ASCII only).

```basic
PRINT UCASE$("hi")
```

#### LCASE$
Converts to lower case (ASCII only).

```basic
PRINT LCASE$("HI")
```

#### CHR$
Returns the character for a code point.

```basic
PRINT CHR$(65)
```

#### ASC
Returns the code point of the first character.

```basic
PRINT ASC("A")
```

#### VAL
Parses a numeric string to a value.

```basic
PRINT VAL("42")
```

#### STR$
Converts a number to a string.

```basic
PRINT STR$(42)
```

#### Math functions

| Function | Example | Result |
|----------|---------|--------|
| `ABS(x)` | `ABS(-3)` | `3` |
| `SQR(x)` | `SQR(9)` | `3` |
| `FLOOR(x)` | `FLOOR(2.7)` | `2` |
| `CEIL(x)` | `CEIL(2.1)` | `3` |
| `POW(x, y)` | `POW(2,3)` | `8` |
| `SIN(x)` | `SIN(0)` | `0` |
| `COS(x)` | `COS(0)` | `1` |
| `RND()` | `RND()` | pseudo-random number |

### Debugging options
Use command-line flags with the compiler:

```bash
ilc --trace=src program.bas
ilc --break main.bas:10 --watch X program.bas
```

## Examples

### Language basics
- [`ex1_hello_cond.bas`](../examples/basic/ex1_hello_cond.bas) prints a greeting and branches on a condition.
- [`ex2_sum_1_to_10.bas`](../examples/basic/ex2_sum_1_to_10.bas) evaluates an expression at compile time.
- [`ex3_for_table.bas`](../examples/basic/ex3_for_table.bas) builds a multiplication table with `FOR` loops.
- [`ex4_if_elseif.bas`](../examples/basic/ex4_if_elseif.bas) shows multi-branch `IF` statements.
- [`ex5_input_echo.bas`](../examples/basic/ex5_input_echo.bas) prompts the user with `INPUT`.
- [`ex6_array_sum.bas`](../examples/basic/ex6_array_sum.bas) accumulates values stored in an array.

### Math and randomness
- [`math_basics.bas`](../examples/basic/math_basics.bas) demonstrates arithmetic, `ABS`, and `SQR`.
- [`math_constfold.bas`](../examples/basic/math_constfold.bas) highlights constant folding of numeric expressions.
- [`sine_cosine.bas`](../examples/basic/sine_cosine.bas) evaluates `SIN` and `COS`.
- [`random_repro.bas`](../examples/basic/random_repro.bas) shows deterministic seeding for `RND()`.
- [`random_walk.bas`](../examples/basic/random_walk.bas) simulates a random walk.
- [`monte_carlo_pi.bas`](../examples/basic/monte_carlo_pi.bas) estimates π with Monte Carlo sampling.

### Procedures and recursion
- [`fact.bas`](../examples/basic/fact.bas) implements factorial with recursion.
- [`fib.bas`](../examples/basic/fib.bas) computes Fibonacci numbers with a loop.
- [`string_builder.bas`](../examples/basic/string_builder.bas) builds strings in a `SUB` and returns the result.
- [`trace_src.bas`](../examples/basic/trace_src.bas) is useful with `ilc --trace=src` for debugging walkthroughs.

### String utilities
- [`strings/len.bas`](../examples/basic/strings/len.bas) measures string length with `LEN`.
- [`strings/left_right.bas`](../examples/basic/strings/left_right.bas) slices characters with `LEFT$` and `RIGHT$`.
- [`strings/mid.bas`](../examples/basic/strings/mid.bas) extracts substrings with `MID$`.
- [`strings/instr.bas`](../examples/basic/strings/instr.bas) searches for substrings with `INSTR`.
- [`strings/trims.bas`](../examples/basic/strings/trims.bas) compares `LTRIM$`, `RTRIM$`, and `TRIM$`.
- [`strings/case.bas`](../examples/basic/strings/case.bas) normalizes case with `UCASE$` and `LCASE$`.
- [`strings/chr_asc.bas`](../examples/basic/strings/chr_asc.bas) converts between characters and code points.
- [`strings/val_str.bas`](../examples/basic/strings/val_str.bas) converts between numeric strings and values.

### Common tasks
- [`strings/ext.bas`](../examples/basic/strings/ext.bas) extracts file extensions.
- [`strings/trim_input.bas`](../examples/basic/strings/trim_input.bas) cleans user input.
- [`strings/normalize.bas`](../examples/basic/strings/normalize.bas) normalizes string case.
- [`strings/csv.bas`](../examples/basic/strings/csv.bas) builds a CSV line.

Sources:
- `docs/basic-language.md`
- `archive/docs/references/basic.md`
