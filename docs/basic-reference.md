---
status: active
audience: public
last-verified: 2026-04-05
---

# Viper BASIC — Reference

Complete language reference for Viper BASIC. This document describes **statements**, **expressions & operators**, *
*built-in functions**, **object features**, and **I/O**. For a tutorial introduction, see *
*[BASIC Tutorial](basic-language.md)**.

---

## Key Language Features

- **Assignment**: Requires `LET` keyword (e.g., `LET X = 2`)
- **Function calls**: Use `Name(args)` with parentheses (required in expressions). For statement-form
  SUB calls, parentheses are required when passing arguments; the legacy paren-less form for zero-arg
  SUBs is accepted in statement position.
- **Built-ins**: Must be called with parentheses, even with zero arguments (`RND()`, `INKEY$()`)
- **Arrays**: One-dimensional, zero-based; require `DIM` or `REDIM`
- **Short-circuit operators**: `ANDALSO` and `ORELSE` (vs. `AND` and `OR`)
- **Functions**: Return values with `RETURN`; subroutines use `SUB`
- **Objects**: `CLASS`, methods, fields, `ME`, `NEW`, `DELETE`, optional `DESTRUCTOR`
- **Enums**: `ENUM...END ENUM` blocks with named integer constants and dot-notation access

---

## Table of Contents

**Language Core**
- [Statements A–Z](#statements-az)
- [Expressions & Operators](#expressions--operators)
- [Built-in Functions](#built-in-functions)

**Object-Oriented Programming**
- [OOP Semantics](#oop-semantics) — Inheritance, interfaces, properties, destructors
- [Runtime Classes (Viper.*)](#runtime-classes-viper)
- [Runtime Classes Usage Examples](#runtime-classes-usage-examples)

**Modules & Namespaces**
- [ViperLib & Namespaces](#viperlib--namespaces)
- [Reserved Root](#reserved-root)
- [Namespaces & USING](#namespaces--using)

**Quick Reference**
- [Keyword Index](#keyword-index)

---

## Statements A–Z

### '

Comment. A leading apostrophe starts a comment.

```basic
' Single-line comment
PRINT "Hello"
```

### CLASS / NEW / DELETE

Defines classes with scalar or array fields; construct with NEW, optionally free with DELETE. Array fields may be
declared with dimensions and accessed using `obj.field(index)`.

```basic
CLASS Counter
  X AS INTEGER
  SUB NEW()
   LET ME.X = 0
  END SUB
  SUB INC()
   LET ME.X = ME.X + 1
  END SUB
END CLASS

DIM c AS Counter
LET c = NEW Counter()
c.INC()
PRINT c.X
DELETE c
```

Array fields:

```basic
CLASS Buffer
  DIM data(8) AS INTEGER
END CLASS

DIM b AS Buffer
LET b = NEW Buffer()      ' data() allocated length 8 by constructor
LET b.data(0) = 42
PRINT b.data(0)
```

Notes:

- When an array field includes dimensions in the class definition, the constructor allocates the array to the specified
  length.
- String array fields are supported; element loads/stores retain/release strings automatically.

### BEEP

Emits a beep or bell sound.

```basic
BEEP
PRINT "Alert!"
```

### CLS

Clears the screen and moves the cursor home (1,1). No-op when stdout is not a TTY.

```basic
CLS
PRINT "Clean screen"
```

### CLOSE

Closes an open file number.

```basic
OPEN "out.txt" FOR OUTPUT AS #1
PRINT #1, "Hello"
CLOSE #1
```

### COLOR

Sets terminal foreground and background colors. Uses values 0-7 for normal colors, 8-15 for bright colors, or -1 to
leave unchanged.

```basic
COLOR 15, 1   ' Bright white on blue
PRINT "Colored text"
COLOR -1, -1  ' Reset to defaults
```

### DESTRUCTOR

Optional destructor called on DELETE and finalization.

```basic
CLASS WithFile
  FH AS INTEGER
  SUB NEW()
   OPEN "out.txt" FOR OUTPUT AS #1
  END SUB
  DESTRUCTOR()
   CLOSE #1
  END DESTRUCTOR
END CLASS
```

### DIM

Declares a variable or array. Required for arrays; optional for scalars (to pin type).

```basic
DIM A(5)           ' array 0..4
DIM Flag AS BOOLEAN
```

### DO ... LOOP

Loop with condition at start or end (DO WHILE/UNTIL … LOOP or DO … LOOP UNTIL/WHILE).

```basic
LET I = 3
DO
  LET I = I - 1
LOOP UNTIL I = 0
```

### END

Terminates program execution immediately.

```basic
PRINT "before end"
END
PRINT "this never prints"
```

### ENUM

Declares a named set of integer constants. Variants are auto-numbered from 0, or may specify explicit values.

```basic
ENUM Direction
  NORTH           ' 0
  SOUTH           ' 1
  EAST            ' 2
  WEST            ' 3
END ENUM

LET d = Direction.NORTH
```

Explicit and mixed values:

```basic
ENUM Priority
  LOW             ' 0
  MEDIUM = 5      ' 5
  HIGH            ' 6
  CRITICAL        ' 7
END ENUM
```

Negative values are supported (`BACKWARD = -1`). Duplicate variant names produce a compile error.

### EXIT

Exits the nearest loop (EXIT FOR / EXIT DO).

```basic
FOR I = 1 TO 10
  IF I = 3 THEN EXIT FOR
  PRINT I
NEXT
```

### FOR ... NEXT

Counted loop; optional STEP.

```basic
FOR I = 1 TO 5
  PRINT I
NEXT
```

### GOSUB

Subroutine call to a line label; returns with RETURN (statement-form).

```basic
GOSUB MySub
PRINT "back"
END
MySub:
PRINT "in subroutine"
RETURN
```

---

## Runtime Classes (Viper.*)

Runtime classes are built-in types provided by the Viper runtime and exposed under the `Viper.*` namespace. They behave
like objects with properties and methods but are implemented by canonical runtime extern functions. The receiver (
instance) is passed as the first argument when lowering to the runtime.

- Properties/methods lower to canonical `Viper.*` externs with the receiver as arg0.
- Behavior and traps match the underlying runtime helpers.
- BASIC `STRING` is an alias of `Viper.String`.
- Optional `NEW` is available when a constructor helper is defined.

### Example: `Viper.String`

```basic
DIM s AS Viper.String
LET s = "hello"
PRINT s.Length              ' prints 5
PRINT s.Substring(1, 3)     ' zero-based start, length → "ell"
PRINT s.Mid(1)              ' suffix from index 1 → "ello"
LET s2 = NEW Viper.String("abc")  ' optional: requires ctor helper
```

Lowering equivalence (receiver as first argument):

- `s.Length` → `Viper.String.get_Length(s)`
- `s.Substring(i, n)` → `Viper.String.Substring(s, i, n)`
- `s.Mid(i)` → `Viper.String.Mid(s, i)` — suffix from position i (no length)
- `NEW Viper.String(x)` → `Viper.String.FromStr(x)` (when available)

Null and bounds:

- Accessing a property/method on a null receiver traps.
- `Substring` traps on invalid inputs consistent with runtime rules; zero-length results return the empty string.

BASIC `STRING` alias:

```basic
DIM s AS STRING
LET s = "abcd"
PRINT s.Length   ' same as Viper.String
```

### `Viper.String` API

Properties:

- `Length: i64` → `Viper.String.get_Length(string)`
- `IsEmpty: i1` → `Viper.String.get_IsEmpty(string)`

Methods:

- `Substring(i64 start, i64 length) -> string` → `Viper.String.Substring(string, i64, i64)`
- `Mid(i64 start) -> string` → `Viper.String.Mid(string, i64)` — suffix from start (no length)
- `Concat(string other) -> string` → `Viper.String.Concat(string, string)`

Constructor helper (optional):

- `FromStr(string s) -> string` → `Viper.String.FromStr(string)`

### `Viper.Collections.List` (non-generic)

Canonical, non-generic list that stores object references (opaque `obj`). Type safety is enforced by user code; the
runtime does not check element types.

Properties:

- `Len: i64` → `Viper.Collections.List.get_Length(obj)`

Methods:

- `Push(obj value) -> void` → `Viper.Collections.List.Push(obj,obj)`
- `Clear() -> void` → `Viper.Collections.List.Clear(obj)`
- `RemoveAt(i64 index) -> void` → `Viper.Collections.List.RemoveAt(obj,i64)`
- `Get(i64 index) -> obj` → `Viper.Collections.List.Get(obj,i64)`
- `Set(i64 index, obj value) -> void` → `Viper.Collections.List.Set(obj,i64,obj)`

Semantics:

- Indexes are zero-based. Negative or out-of-range indexes trap at runtime (bounds error).
- `Clear()` resets `Len` to 0.
- Elements are stored as opaque references; no automatic type conversions are performed.

### GOTO

Unconditional jump to a label.

```basic
GOTO Skip
PRINT "skipped"
Skip:
PRINT "landed"
```

### IF ... THEN

Conditional execution; optional ELSEIF / ELSE; terminated by END IF.

```basic
IF X = 0 THEN
  PRINT "zero"
ELSEIF X < 0 THEN
  PRINT "negative"
ELSE
  PRINT "positive"
END IF
```

### INPUT

Reads tokens from standard input into variables.

```basic
INPUT "Name? ", N$
PRINT "Hello, "; N$
```

### LET

Assignment to a variable, array element, or field. Required for assignment statements.

```basic
LET X = 2
DIM A(3)
LET A(0) = X
```

### LINE INPUT

Reads an entire line into a string variable.

```basic
LINE INPUT "Line? ", L$
PRINT "You typed: "; L$
```

### LOCATE

Moves the terminal cursor to a 1-based row and column position. No-op when stdout is not a TTY.

```basic
CLS
LOCATE 10, 20
PRINT "Centered"
```

### ON ERROR GOTO

Installs an error handler at a label.

```basic
ON ERROR GOTO ErrHandler
OPEN "missing.txt" FOR INPUT AS #1
PRINT "opened"
END
ErrHandler:
PRINT "failed to open"
RESUME 0
```

### TRY … CATCH

Structured error handling that scopes a handler to a lexical block.

Syntax:

```basic
TRY
  ' protected statements
CATCH [errVar]
  ' handler statements
END TRY
```

Behavior:

- Errors raised in the TRY body transfer to the CATCH block.
- After the CATCH block finishes, execution continues after `END TRY`.
- `errVar` (optional) is a local INTEGER (i64) visible only within the CATCH block.
- Inside CATCH, `ERR()` returns the error code (same meaning as in `ON ERROR` handlers).
- Nested TRY/CATCH is allowed; handlers stack in last-in–first-out order.
- Interaction with `ON ERROR GOTO`: a TRY installs a handler on top of any existing handler and pops it at `END TRY` (
  the outer `ON ERROR` handler remains active).

Examples

Divide by zero is caught and control resumes after the block:

```basic
TRY
  LET Z = 0
  LET X = 1 / Z
  PRINT "nope"
CATCH e
  PRINT "caught "; STR$(ERR())
END TRY
PRINT "after"
```

Output:

```text
caught 0
after
```

Nested TRY where the inner handler fires and does not leak outward:

```basic
TRY
  TRY
   OPEN "missing.txt" FOR INPUT AS #1
   PRINT "opened"
  CATCH
   PRINT "inner"
  END TRY
  PRINT "outer-body"
CATCH
PRINT "outer"
END TRY
```

Output:

```text
inner
outer-body
```

### OPEN

Opens a file and assigns it a file number (#).

```basic
OPEN "out.txt" FOR OUTPUT AS #1
PRINT #1, "Hello"
CLOSE #1
```

### PRINT

Writes to the console. ';' suppresses newline; ',' aligns to columns.

```basic
PRINT "Hello, world"
PRINT "A"; "B"      ' prints AB (no newline between)
PRINT "A", "B"      ' prints in columns
```

### PRINT #

Writes to a file using PRINT formatting.

```basic
OPEN "log.txt" FOR OUTPUT AS #1
PRINT #1, "Started"
CLOSE #1
```

### RANDOMIZE

Seeds the random number generator with a given value or current time.

```basic
RANDOMIZE 12345    ' Use specific seed
PRINT RND()        ' Reproducible sequence
```

```basic
RANDOMIZE TIMER    ' Seed from current time
PRINT RND()        ' Different each run
```

### REDIM

Resizes an existing array (contents may be reinitialized).

```basic
DIM A(2)           ' 0..1
REDIM A(10)        ' 0..9
```

### RESUME

Resumes execution after an error; forms: RESUME, RESUME NEXT, RESUME 0.

```basic
PRINT "failed to open"
RESUME 0
```

### RETURN

Returns from a FUNCTION to its caller, or from a GOSUB subroutine to its call site.

```basic
FUNCTION F(N)
  IF N < 0 THEN RETURN -1
  RETURN N * 2
END FUNCTION
```

```basic
GOSUB MySub
PRINT "back"
END
MySub:
PRINT "in subroutine"
RETURN   ' RETURN with no value in GOSUB context
```

### SEEK

Sets or queries the file position for a file number.

```basic
OPEN "data.bin" FOR BINARY AS #1
SEEK #1, 0        ' go to start
CLOSE #1
```

### SELECT CASE

Multi-way branch on a value; range and relational cases supported.

```basic
SELECT CASE N
CASE < 0: PRINT "neg"
CASE 0:   PRINT "zero"
CASE 1 TO 9: PRINT "small"
CASE ELSE: PRINT "big"
END SELECT
```

### SUB / FUNCTION

Declares procedures and functions. Functions return a value via RETURN.

```basic
SUB HELLO(S$)
  PRINT "Hello, "; S$
END SUB

FUNCTION SQUARE(N)
  RETURN N * N
END FUNCTION

HELLO("Ada")            ' statement call (parentheses required)
LET X = SQUARE(9)       ' function in expression
```

### WHILE ... WEND

Loop while a condition is true.

```basic
LET I = 0
WHILE I < 3
  PRINT I
  LET I = I + 1
WEND
```

### WRITE #

Writes comma-delimited data with quotes, to a file number.

```basic
OPEN "out.csv" FOR OUTPUT AS #1
WRITE #1, 1, "two", 3.0
CLOSE #1
```

## Expressions & operators

- Arithmetic: `+ - * / \` (integer division), `MOD`
- Comparison: `= <> < <= > >=`
- Booleans: `NOT`, `AND`, `OR`, **`ANDALSO`**, **`ORELSE`** (short‑circuit)
- String concatenation: `+`

**Precedence (high → low)**: unary (`NOT`), `* / \ MOD`, `+ -`, comparisons, `ANDALSO/ORELSE`, `AND/OR`.

## Built-in functions

The following built-ins are available. Use them in expressions (e.g., `LET X = ABS(-3)`).

| Name    | Args | Returns |
|---------|------|---------|
| ABS     | 1    | Numeric |
| ASC     | 1    | Integer |
| ATN     | 1    | Float   |
| CDBL    | 1    | Float   |
| CEIL    | 1    | Numeric |
| CHR$    | 1    | String  |
| CINT    | 1    | Integer |
| CLNG    | 1    | Integer |
| COS     | 1    | Float   |
| CSNG    | 1    | Float   |
| EOF     | 1    | Integer |
| ERR     | 0    | Integer |
| EXP     | 1    | Float   |
| FIX     | 1    | Numeric |
| FLOOR   | 1    | Numeric |
| GETKEY$ | 0    | String  |
| INKEY$  | 0    | String  |
| INSTR   | 2–3  | Integer |
| INT     | 1    | Numeric |
| LCASE$  | 1    | String  |
| LEFT$   | 2    | String  |
| LEN     | 1    | Integer |
| LOC     | 1    | Integer |
| LOF     | 1    | Integer |
| LOG     | 1    | Float   |
| LTRIM$  | 1    | String  |
| MID$    | 2–3  | String  |
| POW     | 2    | Numeric |
| RIGHT$  | 2    | String  |
| RND     | 0    | Float   |
| ROUND   | 1–2  | Numeric |
| RTRIM$  | 1    | String  |
| SGN     | 1    | Integer |
| SIN     | 1    | Float   |
| SQR     | 1    | Float   |
| STR$    | 1    | String  |
| TAN     | 1    | Float   |
| TIMER   | 0    | Float   |
| TRIM$   | 1    | String  |
| UCASE$  | 1    | String  |
| VAL     | 1    | Float   |

### Built-in examples

**Numeric**

```basic
PRINT ABS(-3)         ' 3
PRINT SQR(9)          ' 3
PRINT INT(3.9)        ' 3
PRINT FIX(-3.9)       ' -3
PRINT ROUND(2.6)      ' 3
PRINT FLOOR(2.6)      ' 2
PRINT CEIL(2.1)       ' 3
PRINT POW(2, 10)      ' 1024
PRINT SIN(0), COS(0)  ' 0  1
PRINT TAN(0)          ' 0
PRINT ATN(1) * 4      ' 3.14159... (pi)
PRINT EXP(1)          ' 2.71828... (e)
PRINT LOG(2.71828)    ' ~1  (natural log)
PRINT SGN(-5)         ' -1
PRINT SGN(0)          ' 0
PRINT TIMER           ' seconds since midnight
PRINT ERR()           ' current error code (0 = none)
```

**String**

```basic
PRINT LEN("abc")            ' 3
PRINT LEFT$("hello", 2)     ' "he"
PRINT RIGHT$("hello", 3)    ' "llo"
PRINT MID$("hello", 2, 2)   ' "el"
PRINT INSTR("banana", "na") ' 3
PRINT LTRIM$("  hi")        ' "hi"
PRINT RTRIM$("hi  ")        ' "hi"
PRINT TRIM$("  hi  ")       ' "hi"
PRINT UCASE$("hi")          ' "HI"
PRINT LCASE$("HI")         ' "hi"
PRINT CHR$(65)             ' "A"
PRINT ASC("A")             ' 65
```

**Conversion & random**

```basic
PRINT CINT(3.9)             ' 4
PRINT CLNG(3.9)             ' 4
PRINT CSNG(3.5)             ' 3.5
PRINT CDBL(3.5)             ' 3.5
PRINT VAL("42")             ' 42
PRINT STR$(42)              ' " 42"
PRINT RND()                 ' 0 <= x < 1
```

**Keyboard**

```basic
LET K$ = INKEY$()
IF K$ = "" THEN K$ = GETKEY$()
PRINT "Key: "; K$
```

**File query**

```basic
OPEN "in.txt" FOR INPUT AS #1
PRINT EOF(#1)         ' 0 until end of file
PRINT LOF(#1)         ' file length in bytes
PRINT LOC(#1)         ' current byte position
CLOSE #1
```

## ViperLib & Namespaces

ViperLib exposes procedures and types under the reserved `Viper.*` root namespace. You can call
procedures fully qualified, or import a namespace with `USING`.

- Fully qualified:

```basic
Viper.Terminal.PrintI64(42)
```

- With USING (same line using `:`):

```basic
USING Viper.Terminal : PrintI64(42)
```

- Or as two lines:

```basic
USING Viper.Terminal
PrintI64(42)
```

### Runtime Procedures

All runtime procedures are available under canonical `Viper.*` namespace names. Legacy `rt_*` aliases are maintained for
compatibility. Signatures shown as `Name(params)->result`.

#### Viper.Terminal

Console I/O operations:

- `Viper.Terminal.PrintI64(i64)->void` — Print an integer to console
- `Viper.Terminal.PrintF64(f64)->void` — Print a floating-point number to console
- `Viper.Terminal.PrintStr(str)->void` — Print a string to console
- `Viper.Terminal.ReadLine()->str?` — Read a line from console input (returns null on EOF)

#### Viper.String

String manipulation:

- `Viper.String.get_Length(str)->i64` — Get string length
- `Viper.String.Mid(str, i64, i64)->str` — Extract substring (start, length)
- `Viper.String.Concat(str, str)->str` — Concatenate two strings
- `Viper.String.SplitFields(str, ptr str, i64)->i64` — Split string into fields
- `Viper.String.FromI16(i16)->str` — Convert int16 to string
- `Viper.String.FromI32(i32)->str` — Convert int32 to string
- `Viper.String.FromSingle(f64)->str` — Convert float to string (formats with single precision)
- `Viper.Text.StringBuilder.New()->ptr` — Create a new StringBuilder instance

#### Viper.Convert

Type conversion:

- `Viper.Convert.ToInt64(str)->i64` — Convert string to integer (throws on error)
- `Viper.Convert.ToDouble(str)->f64` — Convert string to double (throws on error)
- `Viper.Convert.ToString_Int(i64)->str` — Convert integer to string
- `Viper.Convert.ToString_Double(f64)->str` — Convert double to string

#### Viper.Parse

Type parsing (with explicit error handling):

- `Viper.Parse.Int64(cstr, ptr i64)->i32` — Parse int64, returns success code
- `Viper.Parse.Double(cstr, ptr f64)->i32` — Parse double, returns success code

#### Viper.Diagnostics

Error and diagnostic utilities:

- `Viper.Diagnostics.Trap(str)->void` — Trigger a runtime trap with message

### Runtime Types

ViperLib classes are recognized under `Viper.*`. These namespaced runtime types are known to the compiler for
declarations and construction. Their method surfaces are being exposed progressively.

#### Viper

Core types:

- `Viper.Object` — Base class for all objects
- `Viper.String` — Managed string type

#### Viper.Text

Text processing types:

- `Viper.Text.StringBuilder` — Mutable string builder (can be constructed with NEW)

#### Viper.IO

I/O types:

- `Viper.IO.File` — File operations class

#### Viper.Collections

Collection types:

- `Viper.Collections.List` — Dynamic list container

### Examples

Using runtime procedures:

```basic
USING Viper.Terminal
USING Viper.String

PrintStr("Length: ")
PrintI64(Len("hello"))
PrintStr(Concat("hello", " world"))
```

Using runtime types:

```basic
DIM sb AS Viper.Text.StringBuilder
LET sb = NEW Viper.Text.StringBuilder()
```

### Migration Note

Legacy `rt_*` function names (e.g., `rt_print_str`, `rt_str_len`) are maintained as aliases to their canonical `Viper.*`
counterparts. New code should use the canonical names.

## Reserved Root

The `Viper` root namespace is reserved for ViperLib. User code may not declare symbols under `Viper` (for
example, `NAMESPACE Viper.Tools` is an error). You may import and call `Viper.*` library procedures, but define your own
symbols under a different root.

## Namespaces & USING

**IMPORTANT**: USING directives are **file-scoped only** and must appear at the top of the file before any declarations.
USING directives cannot appear inside NAMESPACE, CLASS, or INTERFACE blocks.

### Correct Usage

```basic
' ✓ CORRECT: USING at file scope, before declarations
USING Viper.Terminal

NAMESPACE App
  SUB Main()
    ' Use imported namespace without qualification
    PrintI64(99)
    ' Or fully qualified (always works)
    Viper.Terminal.PrintI64(99)
  END SUB
END NAMESPACE

App.Main()
```

### Incorrect Usage

```basic
' ✗ WRONG: USING after NAMESPACE declaration (Error: E_NS_005)
NAMESPACE App
  SUB Main()
  END SUB
END NAMESPACE

USING Viper.Terminal  ' Error: USING must appear before all declarations
```

```basic
' ✗ WRONG: USING inside NAMESPACE block (Error: E_NS_008)
NAMESPACE App
  USING Viper.Terminal  ' Error: USING inside NAMESPACE block not allowed
  SUB Main()
  END SUB
END NAMESPACE
```

### USING Rules

1. **File scope only**: USING must be at file scope, not inside any block
2. **Before declarations**: USING must appear before any NAMESPACE, CLASS, or INTERFACE declarations
3. **File-scoped effect**: Each file's USING directives do not affect other compilation units
4. **Two forms**:
    - Simple: `USING Viper.Terminal` (imports all from namespace)
    - Aliased: `USING VC = Viper.Terminal` (creates shorthand alias)

For complete namespace documentation, see [Namespace Reference](basic-namespaces.md).

## Keyword index

(All keywords are case-insensitive.)

### A

- `ABS`
- `ABSTRACT`
- `ADDFILE`
- `ADDRESSOF`
- `ALTSCREEN`
- `AND`
- `ANDALSO`
- `APPEND`
- `AS`

### B

- `BASE`
- `BEEP`
- `BINARY`
- `BOOLEAN`
- `BYREF`
- `BYVAL`

### C

- `CASE`
- `CATCH`
- `CEIL`
- `CLASS`
- `CLOSE`
- `CLS`
- `COLOR`
- `CONST`
- `COS`
- `CURSOR`

### D

- `DELETE`
- `DESTRUCTOR`
- `DIM`
- `DO`

### E

- `EACH`
- `ELSE`
- `ELSEIF`
- `END`
- `ENUM`
- `EOF`
- `ERROR`
- `EXIT`
- `EXPORT`

### F

- `FALSE`
- `FINAL`
- `FINALLY`
- `FLOOR`
- `FOR`
- `FUNCTION`

### G

- `GET`
- `GOSUB`
- `GOTO`

### I

- `IF`
- `IMPLEMENTS`
- `IN`
- `INPUT`
- `INTERFACE`
- `IS`

### L

- `LBOUND`
- `LET`
- `LINE`
- `LOC`
- `LOCATE`
- `LOF`
- `LOOP`

### M

- `ME`
- `MOD`

### N

- `NAMESPACE`
- `NEW`
- `NEXT`
- `NOT`
- `NOTHING`

### O

- `OFF`
- `ON`
- `OPEN`
- `OR`
- `ORELSE`
- `OUTPUT`
- `OVERRIDE`

### P

- `POW`
- `PRESERVE`
- `PRINT`
- `PRIVATE`
- `PROPERTY`
- `PUBLIC`

### R

- `RANDOM`
- `RANDOMIZE`
- `REDIM`
- `RESUME`
- `RETURN`
- `RND`

### S

- `SEEK`
- `SELECT`
- `SET`
- `SHARED`
- `SIN`
- `SLEEP`
- `SQR`
- `STATIC`
- `STEP`
- `SUB`
- `SWAP`

### T

- `THEN`
- `TO`
- `TRUE`
- `TRY`
- `TYPE`

### U

- `UBOUND`
- `UNTIL`
- `USING`

### V

- `VIRTUAL`

### W

- `WEND`
- `WHILE`
- `WRITE`

### TRY/CATCH and ON ERROR Interop

TRY/CATCH composes with legacy `ON ERROR GOTO` as a nested handler:

- Precedence: a TRY installs a fresh error handler on top of any active `ON ERROR GOTO` handler.
- Restoration: when the TRY region finishes without error, the TRY handler is popped and the prior `ON ERROR GOTO`
  handler remains in effect.
- Catch resumption: the CATCH body terminates by resuming to the first block after the TRY via a resume token. A
  `RESUME` inside a CATCH is allowed but typically unnecessary because the compiler emits a `resume.label` to the join
  point.

Example:

```basic
ON ERROR GOTO Outer
TRY
  ' protected code
CATCH err
  ' handle inner error (err is i64 here)
END TRY
' Outer ON ERROR handler still active
END

Outer:
RESUME NEXT
```

Semantics:

- Exceptions raised in the TRY body are caught by the TRY handler.
- After `END TRY`, the previously active `ON ERROR GOTO Outer` handler continues to apply.

## Runtime Classes Usage Examples

Runtime-backed classes expose an object surface (properties, methods, constructors) that lower to canonical extern
functions provided by the runtime. Two families are currently available:

- Viper.String (aliased in BASIC as STRING)
- Viper.Text.StringBuilder

These object members are functional equivalents of the procedural helpers under Viper.String.* and Viper.Text.*. The
compiler lowers property and method calls to the corresponding extern with the receiver as argument 0.

Examples:

```basic
DIM s AS STRING                 ' STRING is an alias of Viper.String
LET s = "hello"
Viper.Terminal.PrintI64(s.Length)
Viper.Terminal.PrintStr(s.Substring(2, 3))  ' zero-based start, length 3

DIM sb AS Viper.Text.StringBuilder
LET sb = NEW Viper.Text.StringBuilder()
' Depending on your build, APPEND may be a reserved keyword; use the procedural form below if needed.
' sb.Append("X")
' or equivalently (procedural): sb = Viper.Text.StringBuilder.Append(sb, "X")
Viper.Terminal.PrintI64(sb.Length)
Viper.Terminal.PrintStr(sb.ToString())
```

Conventions and semantics:

- Properties and methods lower to canonical externs with the receiver as arg0.
    - Examples: s.Length → call @Viper.String.Length(s);
      s.Substring(i,n) → call @Viper.String.Substring(s,i,n);
      s.Mid(i) → call @Viper.String.Mid(s,i);
      sb.ToString() → call @Viper.Text.StringBuilder.ToString(sb)
- STRING alias: The BASIC type STRING is the same nominal runtime class as Viper.String.
- Index base: Substring uses the same convention as MID$ — start is 0-based; length is a count.
- Null receivers trap: Accessing a property or method on a null object raises a runtime trap that can be caught with
  TRY/CATCH.
- Procedural equivalence: For every object member there is a procedural helper under Viper.String.* or Viper.Text.*
  with the receiver passed explicitly as the first argument. Use these forms where convenient or when a member name
  collides with a BASIC keyword (e.g., APPEND).

Cross-reference: See [ViperLib & Namespaces](#viperlib--namespaces) for procedural helpers under
Viper.String.* and Viper.Text.*.

---

## OOP Semantics

### Inheritance

Single inheritance only (`CLASS B : A`). Namespaces may qualify base names, e.g. `CLASS B : Foo.Bar.A`.

### Method modifiers

- `VIRTUAL`: Declares a new virtual method. A vtable slot is introduced if the name does not already exist in a base.
- `OVERRIDE`: Reuses the slot of the closest base virtual with the same name. Signature must match exactly.
- `ABSTRACT`: Method has no body; must be implemented in a non-abstract descendant.
- `FINAL`: Prevents further overrides in descendants.
- `PUBLIC|PRIVATE`: Access control enforced at call/field-access sites.

Constructors (`SUB NEW`) may not be marked `VIRTUAL`, `OVERRIDE`, `ABSTRACT`, or `FINAL`.

### Slot assignment

Slot numbering is base-first, stable, and append-only: a class inherits its base's vtable verbatim, each new `VIRTUAL`
appends one entry, and `OVERRIDE` keeps the base slot number. This guarantees deterministic ABI layout.

### Virtual dispatch

- Virtual calls dispatch through the vtable using the receiver's dynamic type.
- Non-virtual methods use direct calls.
- `BASE.M(...)` is a direct call to the immediate base class implementation.

### Interfaces and RTTI

Interfaces are nominal: the name identifies the contract. A class lists one or more interface names on its declaration.

```basic
INTERFACE I
  SUB Speak()
  FUNCTION F() AS I64
END INTERFACE

CLASS A IMPLEMENTS I
  OVERRIDE SUB Speak(): END SUB
  OVERRIDE FUNCTION F() AS I64: RETURN 0: END FUNCTION
END CLASS
```

Each interface assigns slot indices to members in declaration order. A class that implements multiple interfaces has one
itable per interface. Dispatch materializes the callee pointer from the itable and emits `call.indirect`.

RTTI operators:
- `expr IS Class` — true iff the dynamic type equals or derives from the class.
- `expr IS Interface` — true iff the dynamic type implements the interface.
- `expr AS Class` — returns the object when IS succeeds; otherwise NULL.
- `expr AS Interface` — returns the object when IS succeeds; otherwise NULL.

### Static members

- `STATIC SUB NEW()` runs once per class during module initialization before user code.
- Static fields lower to module-scope globals; reads/writes are independent of any instance.
- Static methods do not receive `ME`; referencing `ME` in a static method is a semantic error.

### Properties

A `PROPERTY` defines up to two accessors (`GET` and `SET`). Accessor-level access modifiers are supported.

Property sugar:
- `x.Name` → `get_Name(x)` ; `x.Name = v` → `set_Name(x, v)` (instance)
- `C.Name` → `get_Name()` ; `C.Name = v` → `set_Name(v)` (static)

### Destructors and disposal

- Chaining order: derived body runs first, then base, continuing up the chain.
- One instance destructor per class; no parameters or return value.
- `STATIC DESTRUCTOR` runs at program shutdown in class declaration order.
- `DISPOSE expr` invokes the derived→base destructor chain and releases storage when retain count drops to zero.
- Disposing `NULL` is a no-op; double dispose traps in debug builds.

### Overload resolution

Conversions: only widening numeric (INTEGER→DOUBLE) are permitted implicitly. For each viable candidate, parameters are
scored positionally: exact match (+2), widening numeric (+1), otherwise not viable. The best total score wins; ties are
ambiguous. Property accessors (`get_*/set_*`) participate as ordinary candidates.
