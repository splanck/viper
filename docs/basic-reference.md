---
status: active
audience: public
last-updated: 2026-01-15
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

---

## Table of Contents

- [Statements A–Z](#statements-az)
- [Expressions & Operators](#expressions--operators)
- [Built-in Functions](#built-in-functions)
- [Namespaces & USING](#namespaces--using)
- [Standard Library & Namespaces](#standard-library--namespaces)
- [Reserved Root](#reserved-root)
- [Keyword Index](#keyword-index)
- [Runtime Classes (Viper.*)](#runtime-classes-viper)
- [Runtime Classes Usage Examples](#runtime-classes-usage-examples)

---

## Statements A–Z

### '

Comment. A leading apostrophe starts a comment.

```basic
10 ' Single-line comment
20 PRINT "Hello"
```

### CLASS / NEW / DELETE

Defines classes with scalar or array fields; construct with NEW, optionally free with DELETE. Array fields may be
declared with dimensions and accessed using `obj.field(index)`.

```basic
10 CLASS Counter
20   X AS INTEGER
30   SUB NEW()
40     LET ME.X = 0
50   END SUB
60   SUB INC()
70     LET ME.X = ME.X + 1
80   END SUB
90 END CLASS

100 DIM c AS Counter
110 LET c = NEW Counter()
120 c.INC()
130 PRINT c.X
140 DELETE c
```

Array fields:

```basic
10 CLASS Buffer
20   DIM data(8) AS INTEGER
30 END CLASS

40 DIM b AS Buffer
50 LET b = NEW Buffer()      ' data() allocated length 8 by constructor
60 LET b.data(0) = 42
70 PRINT b.data(0)
```

Notes:

- When an array field includes dimensions in the class definition, the constructor allocates the array to the specified
  length.
- String array fields are supported; element loads/stores retain/release strings automatically.

### BEEP

Emits a beep or bell sound.

```basic
10 BEEP
20 PRINT "Alert!"
```

### CLS

Clears the screen and moves the cursor home (1,1). No-op when stdout is not a TTY.

```basic
10 CLS
20 PRINT "Clean screen"
```

### CLOSE

Closes an open file number.

```basic
10 OPEN "out.txt" FOR OUTPUT AS #1
20 PRINT #1, "Hello"
30 CLOSE #1
```

### COLOR

Sets terminal foreground and background colors. Uses values 0-7 for normal colors, 8-15 for bright colors, or -1 to
leave unchanged.

```basic
10 COLOR 15, 1   ' Bright white on blue
20 PRINT "Colored text"
30 COLOR -1, -1  ' Reset to defaults
```

### DESTRUCTOR

Optional destructor called on DELETE and finalization.

```basic
10 CLASS WithFile
20   FH AS INTEGER
30   SUB NEW()
40     OPEN "out.txt" FOR OUTPUT AS #1
50   END SUB
60   DESTRUCTOR()
70     CLOSE #1
80   END DESTRUCTOR
90 END CLASS
```

### DIM

Declares a variable or array. Required for arrays; optional for scalars (to pin type).

```basic
10 DIM A(5)           ' array 0..4
20 DIM Flag AS BOOLEAN
```

### DO ... LOOP

Loop with condition at start or end (DO WHILE/UNTIL … LOOP or DO … LOOP UNTIL/WHILE).

```basic
10 LET I = 3
20 DO
30   LET I = I - 1
40 LOOP UNTIL I = 0
```

### END

Terminates program execution immediately.

```basic
10 PRINT "before end"
20 END
30 PRINT "this never prints"
```

### EXIT

Exits the nearest loop (EXIT FOR / EXIT DO).

```basic
10 FOR I = 1 TO 10
20   IF I = 3 THEN EXIT FOR
30   PRINT I
40 NEXT
```

### FOR ... NEXT

Counted loop; optional STEP.

```basic
10 FOR I = 1 TO 5
20   PRINT I
30 NEXT
```

### GOSUB

Subroutine call to a line label; returns with RETURN (statement-form).

```basic
10 GOSUB 100
20 PRINT "back"
30 END
100 PRINT "in subroutine"
110 RETURN
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
10 DIM s AS Viper.String
20 LET s = "hello"
30 PRINT s.Length              ' prints 5
40 PRINT s.Substring(1, 3)     ' zero-based start, length → "ell"
50 LET s2 = NEW Viper.String("abc")  ' optional: requires ctor helper
```

Lowering equivalence (receiver as first argument):

- `s.Length` → `Viper.String.get_Length(s)`
- `s.Substring(i, j)` → `Viper.String.Substring(s, i, j)`
- `NEW Viper.String(x)` → `Viper.String.FromStr(x)` (when available)

Null and bounds:

- Accessing a property/method on a null receiver traps.
- `Substring` traps on invalid inputs consistent with runtime rules; zero-length results return the empty string.

BASIC `STRING` alias:

```basic
10 DIM s AS STRING
20 LET s = "abcd"
30 PRINT s.Length   ' same as Viper.String
```

### `Viper.String` API

Properties:

- `Length: i64` → `Viper.String.get_Length(string)`
- `IsEmpty: i1` → `Viper.String.get_IsEmpty(string)`

Methods:

- `Substring(i64 start, i64 length) -> string` → `Viper.String.Substring(string, i64, i64)`
- `Concat(string other) -> string` → `Viper.String.ConcatSelf(string, string)`

Constructor helper (optional):

- `FromStr(string s) -> string` → `Viper.String.FromStr(string)`

### `Viper.Collections.List` (non-generic)

Canonical, non-generic list that stores object references (opaque `obj`). Type safety is enforced by user code; the
runtime does not check element types.

Properties:

- `Len: i64` → `Viper.Collections.List.get_Len(obj)`

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

Unconditional jump to a numeric line label.

```basic
10 GOTO 40
20 PRINT "skipped"
40 PRINT "landed"
```

### IF ... THEN

Conditional execution; optional ELSEIF / ELSE; terminated by END IF.

```basic
10 IF X = 0 THEN
20   PRINT "zero"
30 ELSEIF X < 0 THEN
40   PRINT "negative"
50 ELSE
60   PRINT "positive"
70 END IF
```

### INPUT

Reads tokens from standard input into variables.

```basic
10 INPUT "Name? ", N$
20 PRINT "Hello, "; N$
```

### LET

Assignment to a variable, array element, or field. Required for assignment statements.

```basic
10 LET X = 2
20 DIM A(3)
30 LET A(0) = X
```

### LINE INPUT

Reads an entire line into a string variable.

```basic
10 LINE INPUT "Line? ", L$
20 PRINT "You typed: "; L$
```

### LOCATE

Moves the terminal cursor to a 1-based row and column position. No-op when stdout is not a TTY.

```basic
10 CLS
20 LOCATE 10, 20
30 PRINT "Centered"
```

### ON ERROR GOTO

Installs an error handler at a line label.

```basic
10 ON ERROR GOTO 100
20 OPEN "missing.txt" FOR INPUT AS #1
30 PRINT "opened"
40 END
100 PRINT "failed to open"
110 RESUME 0
```

### TRY … CATCH

Structured error handling that scopes a handler to a lexical block.

Syntax:

```
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
10 TRY
20   LET Z = 0
25   LET X = 1 / Z
30   PRINT "nope"
40 CATCH e
50   PRINT "caught "; STR$(ERR())
60 END TRY
70 PRINT "after"
```

Output:

```
caught 0
after
```

Nested TRY where the inner handler fires and does not leak outward:

```basic
10 TRY
20   TRY
30     OPEN "missing.txt" FOR INPUT AS #1
40     PRINT "opened"
50   CATCH
60     PRINT "inner"
70   END TRY
80   PRINT "outer-body"
90 CATCH
100  PRINT "outer"
110 END TRY
```

Output:

```
inner
outer-body
```

### OPEN

Opens a file and assigns it a file number (#).

```basic
10 OPEN "out.txt" FOR OUTPUT AS #1
20 PRINT #1, "Hello"
30 CLOSE #1
```

### PRINT

Writes to the console. ';' suppresses newline; ',' aligns to columns.

```basic
10 PRINT "Hello, world"
20 PRINT "A"; "B"      ' prints AB (no newline between)
30 PRINT "A", "B"      ' prints in columns
```

### PRINT #

Writes to a file using PRINT formatting.

```basic
10 OPEN "log.txt" FOR OUTPUT AS #1
20 PRINT #1, "Started"
30 CLOSE #1
```

### RANDOMIZE

Seeds the random number generator with a given value or current time.

```basic
10 RANDOMIZE 12345    ' Use specific seed
20 PRINT RND()        ' Reproducible sequence
```

```basic
10 RANDOMIZE TIMER    ' Seed from current time
20 PRINT RND()        ' Different each run
```

### REDIM

Resizes an existing array (contents may be reinitialized).

```basic
10 DIM A(2)           ' 0..1
20 REDIM A(10)        ' 0..9
```

### RESUME

Resumes execution after an error; forms: RESUME, RESUME NEXT, RESUME 0.

```basic
100 PRINT "failed to open"
110 RESUME 0
```

### RETURN

Returns from a FUNCTION to its caller.

```basic
10 FUNCTION F(N)
20   IF N < 0 THEN RETURN -1
30   RETURN N * 2
40 END FUNCTION
```

### SEEK

Sets or queries the file position for a file number.

```basic
10 OPEN "data.bin" FOR BINARY AS #1
20 SEEK #1, 0        ' go to start
30 CLOSE #1
```

### SELECT CASE

Multi-way branch on a value; range and relational cases supported.

```basic
10 SELECT CASE N
20 CASE < 0: PRINT "neg"
30 CASE 0:   PRINT "zero"
40 CASE 1 TO 9: PRINT "small"
50 CASE ELSE: PRINT "big"
60 END SELECT
```

### SUB / FUNCTION

Declares procedures and functions. Functions return a value via RETURN.

```basic
10 SUB HELLO(S$)
20   PRINT "Hello, "; S$
30 END SUB

40 FUNCTION SQUARE(N)
50   RETURN N * N
60 END FUNCTION

70 HELLO("Ada")            ' statement call (parentheses required)
80 LET X = SQUARE(9)       ' function in expression
```

### WHILE ... WEND

Loop while a condition is true.

```basic
10 LET I = 0
20 WHILE I < 3
30   PRINT I
40   LET I = I + 1
50 WEND
```

### WRITE #

Writes comma-delimited data with quotes, to a file number.

```basic
10 OPEN "out.csv" FOR OUTPUT AS #1
20 WRITE #1, 1, "two", 3.0
30 CLOSE #1
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
| ABS     | 1    | Depends |
| ASC     | 1    | Integer |
| CDBL    | 1    | Float   |
| CEIL    | 1    | Depends |
| CHR$    | 1    | String  |
| CINT    | 1    | Integer |
| CLNG    | 1    | Integer |
| COS     | 1    | Depends |
| CSNG    | 1    | Float   |
| EOF     | 1    | Integer |
| FIX     | 1    | Depends |
| FLOOR   | 1    | Depends |
| GETKEY$ | 0    | String  |
| INKEY$  | 0    | String  |
| INSTR   | 2–3  | Integer |
| INT     | 1    | Depends |
| LCASE$  | 1    | String  |
| LEFT$   | 2    | String  |
| LEN     | 1    | Integer |
| LOC     | 1    | Integer |
| LOF     | 1    | Integer |
| LTRIM$  | 1    | String  |
| MID$    | 2–3  | String  |
| POW     | 2    | Depends |
| RIGHT$  | 2    | String  |
| RND     | 0    | Depends |
| ROUND   | 1–2  | Depends |
| RTRIM$  | 1    | String  |
| SIN     | 1    | Depends |
| SQR     | 1    | Depends |
| STR$    | 1    | String  |
| TRIM$   | 1    | String  |
| UCASE$  | 1    | String  |
| VAL     | 1    | Float   |

### Built-in examples

**Numeric**

```basic
10 PRINT ABS(-3)         ' 3
20 PRINT SQR(9)          ' 3
30 PRINT INT(3.9)        ' 3
40 PRINT FIX(-3.9)       ' -3
50 PRINT ROUND(2.6)      ' 3
60 PRINT FLOOR(2.6)      ' 2
70 PRINT CEIL(2.1)       ' 3
80 PRINT POW(2, 10)      ' 1024
90 PRINT SIN(0), COS(0)  ' 0  1
```

**String**

```basic
10 PRINT LEN("abc")            ' 3
20 PRINT LEFT$("hello", 2)     ' "he"
30 PRINT RIGHT$("hello", 3)    ' "llo"
40 PRINT MID$("hello", 2, 2)   ' "el"
50 PRINT INSTR("banana", "na") ' 3
60 PRINT LTRIM$("  hi")        ' "hi"
70 PRINT RTRIM$("hi  ")        ' "hi"
80 PRINT TRIM$("  hi  ")       ' "hi"
90 PRINT UCASE$("hi")          ' "HI"
100 PRINT LCASE$("HI")         ' "hi"
110 PRINT CHR$(65)             ' "A"
120 PRINT ASC("A")             ' 65
```

**Conversion & random**

```basic
10 PRINT CINT(3.9)             ' 4
20 PRINT CLNG(3.9)             ' 4
30 PRINT CSNG(3.5)             ' 3.5
40 PRINT CDBL(3.5)             ' 3.5
50 PRINT VAL("42")             ' 42
60 PRINT STR$(42)              ' " 42"
70 PRINT RND()                 ' 0 <= x < 1
```

**Keyboard**

```basic
10 LET K$ = INKEY$()
20 IF K$ = "" THEN K$ = GETKEY$()
30 PRINT "Key: "; K$
```

**File query**

```basic
10 OPEN "in.txt" FOR INPUT AS #1
20 PRINT EOF(#1)         ' 0 until end of file
30 PRINT LOF(#1)         ' file length in bytes
40 PRINT LOC(#1)         ' current byte position
50 CLOSE #1
```

## Standard Library & Namespaces

The Viper standard library exposes procedures and types under the reserved `Viper.*` root namespace. You can call
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
- `Viper.Terminal.ReadLine()->str` — Read a line from console input

#### Viper.String

String manipulation:

- `Viper.String.get_Length(str)->i64` — Get string length
- `Viper.String.Mid(str, i64, i64)->str` — Extract substring (start, length)
- `Viper.String.Concat(str, str)->str` — Concatenate two strings
- `Viper.String.SplitFields(str, ptr str, i64)->i64` — Split string into fields
- `Viper.String.FromI16(i16)->str` — Convert int16 to string
- `Viper.String.FromI32(i32)->str` — Convert int32 to string
- `Viper.String.FromSingle(f32)->str` — Convert float to string
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

Standard library classes are recognized under `Viper.*`. These namespaced runtime types are known to the compiler for
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
USING Viper.Strings

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

The `Viper` root namespace is reserved for the standard library. User code may not declare symbols under `Viper` (for
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

For complete namespace documentation, see [Namespace Reference](devdocs/namespaces.md).

## Keyword index

(All keywords are case-insensitive.)

### A

- `ABS`
- `AND`
- `ANDALSO`
- `APPEND`
- `AS`

### B

- `BINARY`
- `BOOLEAN`

### C

- `CASE`
- `CEIL`
- `CLASS`
- `CLOSE`
- `CLS`
- `COLOR`
- `COS`

### D

- `DELETE`
- `DESTRUCTOR`
- `DIM`
- `DO`

### E

- `ELSE`
- `ELSEIF`
- `END`
- `EOF`
- `ERROR`
- `EXIT`

### F

- `FALSE`
- `FLOOR`
- `FOR`
- `FUNCTION`

### G

- `GOSUB`
- `GOTO`

### I

- `IF`
- `INPUT`

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

- `NEW`
- `NEXT`
- `NOT`

### O

- `ON`
- `OPEN`
- `OR`
- `ORELSE`
- `OUTPUT`

### P

- `POW`
- `PRINT`

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
- `SIN`
- `SQR`
- `STEP`
- `SUB`

### T

- `THEN`
- `TO`
- `TRUE`
- `TYPE`

### U

- `UBOUND`
- `UNTIL`

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

10 ON ERROR GOTO Outer
20 TRY
30   ' protected code
40 CATCH err
50   ' handle inner error (err is i64 here)
60 END TRY
70 ' Outer ON ERROR handler still active
80 END
100 Outer:
110 RESUME NEXT

Semantics:

- Inner exceptions raised between lines 20–39 are caught by the TRY handler.
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
10 DIM s AS STRING                 ' STRING is an alias of Viper.String
20 LET s = "hello"
30 Viper.Terminal.PrintI64(s.Length)
40 Viper.Terminal.PrintStr(s.Substring(2, 3))  ' index base matches MID$: 0-based start

100 DIM sb AS Viper.Text.StringBuilder
110 LET sb = NEW Viper.Text.StringBuilder()
120 ' Depending on your build, APPEND may be a reserved keyword; use the procedural form below if needed.
130 ' sb.Append("X")
140 ' or equivalently (procedural): sb = Viper.Text.StringBuilder.Append(sb, "X")
150 Viper.Terminal.PrintI64(sb.Length)
160 Viper.Terminal.PrintStr(sb.ToString())
```

Conventions and semantics:

- Properties and methods lower to canonical externs with the receiver as arg0.
    - Examples: s.Length → call @Viper.String.Len(s);
      s.Substring(i,n) → call @Viper.String.Mid(s,i,n);
      sb.ToString() → call @Viper.Text.StringBuilder.ToString(sb)
- STRING alias: The BASIC type STRING is the same nominal runtime class as Viper.String.
- Index base: Substring uses the same convention as MID$ — start is 0-based; length is a count.
- Null receivers trap: Accessing a property or method on a null object raises a runtime trap that can be caught with
  TRY/CATCH.
- Procedural equivalence: For every object member there is a procedural helper under Viper.String.* or Viper.Text.*
  with the receiver passed explicitly as the first argument. Use these forms where convenient or when a member name
  collides with a BASIC keyword (e.g., APPEND).

Cross-reference: See [Standard Library & Namespaces](#standard-library--namespaces) for procedural helpers under
Viper.String.* and Viper.Text.*.
