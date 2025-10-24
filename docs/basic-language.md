---
status: draft
audience: public
last-verified: 2025-10-24
---

# Viper BASIC Language Reference (Proposed)

> **Goal:** One canonical, searchable page for everything the Viper BASIC frontend supports—syntax, semantics, built‑ins, OOP—replacing the split across `basic-language.md` and `basic-oop.md`.

## Table of contents
- [Overview](#overview)
- [Program structure](#program-structure)
- [Types and literals](#types-and-literals)
- [Variables, arrays, and assignment](#variables-arrays-and-assignment)
- [Expressions and operators](#expressions-and-operators)
- [Control flow](#control-flow)
- [Procedures](#procedures)
- [Object‑oriented features](#object-oriented-features)
- [Console I/O](#console-io)
- [File I/O](#file-io)
- [Built‑in functions](#built-in-functions)
- [Error handling](#error-handling)
- [Keywords index](#keywords-index)
- [Examples](#examples)

---

## Overview
Viper BASIC is a small, strict, **line‑numbers‑optional** dialect that lowers to Viper IL. It is case‑insensitive and favors clarity over magical behavior. Integers are 64‑bit; floating‑point is IEEE‑754 double (`#` suffix in literals/vars); strings carry a `$` suffix.

> **Design notes**
> - Numeric semantics and traps are described in *Specs → Numerics*. Runtime error conventions are in *Specs → Errors*.
> - Short‑circuit Boolean operators are available: `ANDALSO`, `ORELSE`.

Hello, world:

```basic
PRINT "Hello, Viper!"
```

## Program structure
- Top‑level **statements** run from top to bottom.
- **Labels** end with `:` and are targets of `GOTO`/`GOSUB`/`ON ERROR GOTO`.
- **Procedures** (`SUB`/`FUNCTION`) and **classes** (`CLASS`) may appear at top level.
- **Comments** start with apostrophe `'` or `REM`.

```basic
Start:
  REM Single‑line comment
  ' Also a comment
  PRINT "ok"
  GOTO Done
Done:
  END
```

## Types and literals
- **Integer** (default), **Float** (`#`), **String** (`$`), **Boolean** (`TRUE`/`FALSE`).
- Suffixes: `X#` (float var), `S$` (string var). Explicit typing via `AS`:

```basic
DIM A AS BOOLEAN
DIM PI# : LET PI# = 3.141592653589793#
```

Arrays: one‑dimensional, zero‑based by default.

```basic
DIM A(3)            ' indices 0..2
REDIM A(UBOUND(A)+1)
PRINT LBOUND(A), UBOUND(A)
```

## Variables, arrays, and assignment
- `LET` is optional: `LET X = 1` and `X = 1` are equivalent.
- Bounds are checked at runtime; out‑of‑range indexes trap.
- `LBOUND(A)`, `UBOUND(A)` query indices.

## Expressions and operators
Arithmetic: `+ - * / \ ^ MOD` (integer division is `\`; exponent is `^`).  
Comparison: `= <> < <= > >=`.  
Boolean: `NOT`, `AND`/`OR` (eager), `ANDALSO`/`ORELSE` (short‑circuit).

Strings:
- Concatenation via `+` (with numeric→string conversions via `STR$`).
- Substrings and search via `LEFT$`, `RIGHT$`, `MID$`, `INSTR`.

## Control flow
### IF / ELSEIF / ELSE / END IF
```basic
IF X < 0 THEN
  PRINT "neg"
ELSEIF X = 0 THEN
  PRINT "zero"
ELSE
  PRINT "pos"
END IF
```

### WHILE / WEND
```basic
WHILE N > 0
  PRINT N
  LET N = N - 1
WEND
```

### DO / LOOP
`DO ... LOOP` with `WHILE` or `UNTIL` conditions:

```basic
DO
  LET N = N - 1
LOOP UNTIL N = 0
```

### FOR / NEXT
```basic
FOR I = 1 TO 10 STEP 2
  PRINT I
NEXT I
```

### SELECT CASE / END SELECT
```basic
SELECT CASE ROLL
  CASE 1
    PRINT "one"
  CASE 2,3,4
    PRINT "mid "; ROLL
  CASE ELSE
    PRINT "other"
END SELECT
```

### Jumps
`GOTO Label`, `GOSUB Label`/`RETURN`, `EXIT FOR/DO/WHILE`, `END`.

## Procedures
- `FUNCTION` returns a value with `RETURN expr`.
- `SUB` uses `RETURN` with no value.

```basic
FUNCTION Square(N)
  RETURN N * N
END FUNCTION

SUB Greet(S$)
  PRINT "Hi, "; S$
  RETURN
END SUB
```

## Object‑oriented features
Classes are first‑class: fields, methods, constructors, destructors, `ME`, dot calls.

```basic
CLASS Counter
  value AS INTEGER

  SUB NEW()
    LET value = 0
  END SUB

  SUB Inc()
    LET value = value + 1
  END SUB

  FUNCTION Current() AS INTEGER
    RETURN value
  END FUNCTION

  DESTRUCTOR
    PRINT "destroying"; value
  END DESTRUCTOR
END CLASS

DIM c
LET c = NEW Counter()
c.Inc()
PRINT c.Current()
DELETE c
```

Rules:
- `SUB NEW()` is the constructor; `DESTRUCTOR` runs on `DELETE`.
- Use `ME` inside methods to refer to the instance.
- Member access/calls use dot (`obj.Field`, `obj.Method()`).

## Console I/O
`PRINT` outputs expressions. Use `;` to suppress newline or join fields; `,` separates to print zones.

```basic
PRINT "X=", X
PRINT "A"; 1; "B"
```

`INPUT` reads a value; `LINE INPUT` reads an entire line into a string:

```basic
INPUT N
LINE INPUT A$
```

Terminal helpers: `CLS`, `COLOR fg[, bg]`, `LOCATE row[, col]`.

## File I/O
Open files with a mode and file number, then use channel‑forms of the I/O statements.

```basic
OPEN "data.txt" FOR OUTPUT AS #1
WRITE #1, "A", 42, "B"
CLOSE #1

OPEN "data.txt" FOR INPUT AS #1
LINE INPUT #1, S$
PRINT "EOF start:"; EOF(#1)
PRINT "LOF:"; LOF(#1)
PRINT "LOC:"; LOC(#1)
SEEK #1, 0
CLOSE #1
```

Modes: `INPUT`, `OUTPUT`, `APPEND`, `BINARY`, `RANDOM`.  
Helpers: `EOF(#)`, `LOF(#)` (length), `LOC(#)` (position), `SEEK #, pos`.

## Built‑in functions
| Name | Args | Returns | Notes |
|---|---:|---|---|
| `ABS` | 1 | numeric/string (see notes) |  |
| `ASC` | 1 | Integer |  |
| `CDBL` | 1 | Float | Convert with rounding/trunc per target type. |
| `CEIL` | 1 | numeric/string (see notes) |  |
| `CHR$` | 1 | String |  |
| `CINT` | 1 | Integer | Convert with rounding/trunc per target type. |
| `CLNG` | 1 | Integer | Convert with rounding/trunc per target type. |
| `COS` | 1 | numeric/string (see notes) |  |
| `CSNG` | 1 | Float | Convert with rounding/trunc per target type. |
| `EOF` | 1 | Integer | File functions; use with file number (#n). |
| `FIX` | 1 | numeric/string (see notes) | Truncate toward 0. |
| `FLOOR` | 1 | numeric/string (see notes) |  |
| `GETKEY$` | 0 | String | Keyboard; GETKEY$ blocks, INKEY$ non‑blocking (may return ""). |
| `INKEY$` | 0 | String | Keyboard; GETKEY$ blocks, INKEY$ non‑blocking (may return ""). |
| `INSTR` | 2–3 | Integer | Forms: INSTR(hay$, needle$); INSTR(start, hay$, needle$). 1‑based; 0 if not found. |
| `INT` | 1 | numeric/string (see notes) | Greatest integer ≤ x. |
| `LCASE$` | 1 | String |  |
| `LEFT$` | 2 | String | Takes n characters. |
| `LEN` | 1 | Integer |  |
| `LOC` | 1 | Integer | File functions; use with file number (#n). |
| `LOF` | 1 | Integer | File functions; use with file number (#n). |
| `LTRIM$` | 1 | String |  |
| `MID$` | 2–3 | String | 2‑arg (s$, start) to end; 3‑arg (s$, start, len). 1‑based. |
| `POW` | 2 | numeric/string (see notes) |  |
| `RIGHT$` | 2 | String | Takes n characters. |
| `RND` | 0 | numeric/string (see notes) | Uniform [0,1). Use RANDOMIZE seed to seed. |
| `ROUND` | 1–2 | numeric/string (see notes) | Round to n digits. |
| `RTRIM$` | 1 | String |  |
| `SIN` | 1 | numeric/string (see notes) |  |
| `SQR` | 1 | numeric/string (see notes) |  |
| `STR$` | 1 | String | Converts numeric to string. |
| `TRIM$` | 1 | String |  |
| `UCASE$` | 1 | String |  |
| `VAL` | 1 | Float | Parses leading numeric; ignores trailing junk. |

## Error handling
Structured like classic BASIC:

```basic
ON ERROR GOTO Handler
PRINT #1, 42        ' will error if #1 not open
ON ERROR GOTO 0     ' disable handler
GOTO After

Handler:
  PRINT "caught"
  RESUME NEXT

After:
```

- `ON ERROR GOTO label` installs a handler; `ON ERROR GOTO 0` clears it.
- `RESUME`, `RESUME NEXT` continue after handling.

## Keywords index
**Control Flow:** `CASE`, `DO`, `ELSE`, `ELSEIF`, `END`, `ERROR`, `EXIT`, `FOR`, `GOSUB`, `GOTO`, `IF`, `LOOP`, `NEXT`, `ON`, `RESUME`, `RETURN`, `SELECT`, `STEP`, `THEN`, `TO`, `WEND`, `WHILE`

**I/O (console):** `CLS`, `COLOR`, `INPUT`, `LOC`, `LOCATE`, `PRINT`, `WRITE`

**File I/O:** `APPEND`, `BINARY`, `CLOSE`, `EOF`, `LINE`, `LOF`, `OPEN`, `OUTPUT`, `RANDOM`, `SEEK`

**Ops & Logic:** `AND`, `ANDALSO`, `MOD`, `NOT`, `OR`, `ORELSE`

**Math:** `ABS`, `CEIL`, `COS`, `FLOOR`, `POW`, `RND`, `SIN`, `SQR`

**Types & Decls:** `AS`, `BOOLEAN`, `DIM`, `FALSE`, `FUNCTION`, `LBOUND`, `REDIM`, `SUB`, `TRUE`, `TYPE`, `UBOUND`

**OOP:** `CLASS`, `DELETE`, `DESTRUCTOR`, `ME`, `NEW`

**Other:** `LET`, `RANDOMIZE`, `UNTIL`

## Examples
- **Math and randomness**

```basic
RANDOMIZE 1
LET X# = RND()
PRINT ABS(-5), FLOOR(1.9#), CEIL(1.1#), SQR(9#), POW(2#,10)
```

- **Strings**

```basic
LET F$ = "report.txt"
LET P = INSTR(F$, ".")
PRINT RIGHT$(F$, LEN(F$) - P)

PRINT INSTR("HELLO","LL")
PRINT INSTR(3,"HELLO","L")
```

- **Short‑circuit booleans**

```basic
INPUT A: INPUT B
DIM LHS AS BOOLEAN
LET LHS = A <> 0
PRINT LHS ORELSE (B <> 0)
PRINT LHS ANDALSO (B <> 0)
```

- **File I/O scan**

```basic
OPEN "tmp.txt" FOR OUTPUT AS #1
PRINT #1, "HELLO"
CLOSE #1

OPEN "tmp.txt" FOR INPUT AS #1
LINE INPUT #1, S$
PRINT S$
CLOSE #1
```

---
