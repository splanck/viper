<!--
SPDX-License-Identifier: MIT
File: docs/basic-reference.md
Purpose: Reference for Viper BASIC v0.1 features.
Merged from docs/references/basic.md and docs/reference/basic-language.md on 2025-09-12.
-->

# BASIC Reference

## Table of Contents
- [Program Structure](#program-structure)
- [Data Types](#data-types)
- [Variables and Assignment](#variables-and-assignment)
- [Expressions](#expressions)
- [PRINT](#print)
- [INPUT](#input)
- [Control Flow](#control-flow)
  - [IF / ELSEIF / ELSE / END IF](#if--elseif--else--end-if)
  - [WHILE / WEND](#while--wend)
  - [FOR / NEXT](#for--next)
  - [Multi-statement lines](#multi-statement-lines)
- [Procedures](#procedures)
  - [FUNCTION](#function)
  - [SUB](#sub)
  - [Parameters](#parameters)
- [Arrays](#arrays)
- [Intrinsic Functions](#intrinsic-functions)
  - [Indexing and Substrings](#indexing-and-substrings)
  - [LEN](#len)
  - [LEFT$](#left)
  - [RIGHT$](#right)
  - [MID$](#mid)
  - [INSTR](#instr)
  - [LTRIM$](#ltrim)
  - [RTRIM$](#rtrim)
  - [TRIM$](#trim)
  - [UCASE$](#ucase)
  - [LCASE$](#lcase)
  - [CHR$](#chr)
  - [ASC](#asc)
  - [VAL](#val)
  - [STR$](#str)
  - [Math Functions](#math-functions)
- [Common Tasks](#common-tasks)
  - [Extract file extension](#extract-file-extension)
  - [Trim input](#trim-input)
  - [Normalize case](#normalize-case)
  - [Build a CSV line](#build-a-csv-line)
- [Debugging Options](#debugging-options)
- [What changed in this consolidation](#what-changed-in-this-consolidation)

## Program Structure
Programs can include top-level statements and user-defined `FUNCTION` or `SUB` declarations. Procedures may appear before or after the main program.

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

## Data Types
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

## Variables and Assignment
Variables are created with `LET`. Scalars are named without suffix (int), with `#` (float), or `$` (string).

```basic
10 LET A = 1
20 LET B# = 2.5
30 LET C$ = "OK"
```

## Expressions
Arithmetic operators: `+`, `-`, `*`, `/`, integer division `\\`, `MOD`.

```basic
10 LET X = 5 \\ 2
20 PRINT X MOD 2
```

## PRINT
Outputs comma-separated expressions.

```basic
10 LET X = 1
20 PRINT "X=", X
```

## INPUT
Reads a line into a variable.

```basic
10 INPUT "Name? "; N$
20 PRINT "Hi ", N$
```

## Control Flow

### IF / ELSEIF / ELSE / END IF
```basic
10 IF A = 1 THEN
20   PRINT "one"
30 ELSEIF A = 2 THEN
40   PRINT "two"
50 ELSE
60   PRINT "other"
70 END IF
```

### WHILE / WEND
```basic
10 WHILE I < 3
20   PRINT I
30   LET I = I + 1
40 WEND
```

### FOR / NEXT
```basic
10 FOR I = 1 TO 3
20   PRINT I
30 NEXT I
```

### Multi-statement lines
Separate multiple statements with `:`.

```basic
10 LET A = 1 : PRINT A
```

## Procedures

### FUNCTION
Declare with `FUNCTION name(params)` and end with `END FUNCTION`.

```basic
10 FUNCTION Add(X, Y)
20   RETURN X + Y
30 END FUNCTION
```

### SUB
Declare with `SUB name(params)` and end with `END SUB`.

```basic
10 SUB Hello
20   PRINT "hi"
30 END SUB
```

### Parameters
Parameters are passed by value; arrays are passed by reference.

```basic
10 FUNCTION Inc(X)
20   RETURN X + 1
30 END FUNCTION
40 PRINT Inc(1)
```

## Arrays
Declare with `DIM` and index with parentheses.

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

## Intrinsic Functions

### Indexing and Substrings {#indexing-and-substrings}
String positions are 1-based. Substring functions interpret the starting position as 1 = first character. When a length is supplied, it counts characters from the starting position.

### LEN {#len}
Returns length of a string.

```basic
PRINT LEN("HELLO")
```

**Example** ([`examples/basic/strings/len.bas`](../examples/basic/strings/len.bas))

### LEFT$ {#left}
Returns the leftmost `n` characters.

```basic
PRINT LEFT$("hello", 3)
```

**Example** ([`examples/basic/strings/left_right.bas`](../examples/basic/strings/left_right.bas))

### RIGHT$ {#right}
Returns the rightmost `n` characters.

```basic
PRINT RIGHT$("hello", 2)
```

**Example** ([`examples/basic/strings/left_right.bas`](../examples/basic/strings/left_right.bas))

### MID$ {#mid}
Extracts a substring starting at position.

```basic
PRINT MID$("hello", 2, 2)
```

**Example** ([`examples/basic/strings/mid.bas`](../examples/basic/strings/mid.bas))

### INSTR {#instr}
Finds the first occurrence of `needle$` in `haystack$`.

```basic
PRINT INSTR("HELLO", "LO")
```

**Example** ([`examples/basic/strings/instr.bas`](../examples/basic/strings/instr.bas))

### LTRIM$ {#ltrim}
Removes leading whitespace.

```basic
PRINT LTRIM$("  hi")
```

**Example** ([`examples/basic/strings/trims.bas`](../examples/basic/strings/trims.bas))

### RTRIM$ {#rtrim}
Removes trailing whitespace.

```basic
PRINT RTRIM$("hi  ")
```

**Example** ([`examples/basic/strings/trims.bas`](../examples/basic/strings/trims.bas))

### TRIM$ {#trim}
Removes leading and trailing whitespace.

```basic
PRINT TRIM$("  hi  ")
```

**Example** ([`examples/basic/strings/trims.bas`](../examples/basic/strings/trims.bas))

### UCASE$ {#ucase}
Converts to upper case (ASCII only).

```basic
PRINT UCASE$("hi")
```

**Example** ([`examples/basic/strings/case.bas`](../examples/basic/strings/case.bas))

### LCASE$ {#lcase}
Converts to lower case (ASCII only).

```basic
PRINT LCASE$("HI")
```

**Example** ([`examples/basic/strings/case.bas`](../examples/basic/strings/case.bas))

### CHR$ {#chr}
Returns the character for a code point.

```basic
PRINT CHR$(65)
```

**Example** ([`examples/basic/strings/chr_asc.bas`](../examples/basic/strings/chr_asc.bas))

### ASC {#asc}
Returns the code point of the first character.

```basic
PRINT ASC("A")
```

**Example** ([`examples/basic/strings/chr_asc.bas`](../examples/basic/strings/chr_asc.bas))

### VAL {#val}
Parses a numeric string to a value.

```basic
PRINT VAL("42")
```

**Example** ([`examples/basic/strings/val_str.bas`](../examples/basic/strings/val_str.bas))

### STR$ {#str}
Converts a number to a string.

```basic
PRINT STR$(42)
```

**Example** ([`examples/basic/strings/val_str.bas`](../examples/basic/strings/val_str.bas))

### Math Functions {#math-functions}
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

## Common Tasks

### Extract file extension
**Example** ([`examples/basic/strings/ext.bas`](../examples/basic/strings/ext.bas))

### Trim input
**Example** ([`examples/basic/strings/trim_input.bas`](../examples/basic/strings/trim_input.bas))

### Normalize case
**Example** ([`examples/basic/strings/normalize.bas`](../examples/basic/strings/normalize.bas))

### Build a CSV line
**Example** ([`examples/basic/strings/csv.bas`](../examples/basic/strings/csv.bas))

## Debugging Options
Use command-line flags with the compiler:

```bash
ilc --trace=src program.bas
ilc --break main.bas:10 --watch X program.bas
```

## What changed in this consolidation
This document merges and supersedes:

- `docs/references/basic.md`
- `docs/reference/basic-language.md`
