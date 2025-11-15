---
status: active
audience: public
last-updated: 2025-11-15
---

# Viper BASIC — Tutorial

Learn Viper BASIC by example. For a complete reference, see **[BASIC Reference](basic-reference.md)**.

> **What is Viper BASIC?**
> A compact, modernized BASIC designed for clarity: `LET` for assignment, clean arrays, short-circuit booleans, lightweight objects, and straightforward console/file I/O. It runs on Viper's VM and can be lowered to native code.

---

## Table of Contents

1. [First Steps: Printing and Expressions](#1-first-steps-printing-and-expressions)
2. [Variables and Types](#2-variables-and-types)
3. [Control Flow](#3-control-flow)
4. [Procedures and Functions](#4-procedures-and-functions)
5. [Objects](#5-objects)
6. [Organizing Code with Namespaces](#6-organizing-code-with-namespaces)
7. [Console and File I/O](#7-console-and-file-io)
8. [Error Handling](#8-error-handling)
9. [Example Projects](#9-example-projects)
10. [Where to Go Next](#10-where-to-go-next)

---

## 1. First Steps: Printing and Expressions

```basic
10 PRINT "Hello, world!"
20 PRINT 1 + 2 * 3       ' 7
30 PRINT "A"; "B"        ' "AB" (no newline)
```

**Key points:**
- Line numbers are optional
- Use `:` to put multiple statements on one line
- `'` starts a single-line comment
- `;` in PRINT suppresses the newline between values

---

## 2. Variables and Types

### Assignment with LET

Assignments **require** `LET`. Variables can be declared implicitly by assigning, or explicitly with `DIM` to pin a type.

```basic
10 LET I = 42
20 DIM Flag AS BOOLEAN
30 LET Flag = TRUE
```

### Arrays

Arrays **require** `DIM` and are zero-based:

```basic
10 DIM A(3)              ' indices 0..2
20 LET A(0) = 42
30 LET A(1) = 100
40 PRINT A(0)            ' 42
```

### Type Suffixes

| Suffix | Type    |
|--------|---------|
| `$`    | String  |
| `#`    | Float   |
| `!`    | Float   |
| `&`    | Integer |
| `%`    | Integer |

```basic
10 LET S$ = "hello"
20 LET F# = 3.14
30 LET I% = 42
```

---

## 3. Control Flow

### IF ... THEN ... ELSE

```basic
10 IF N = 0 THEN
20   PRINT "zero"
30 ELSEIF N < 0 THEN
40   PRINT "negative"
50 ELSE
60   PRINT "positive"
70 END IF
```

### FOR ... NEXT

```basic
10 FOR I = 1 TO 5
20   PRINT I
30 NEXT

' With STEP
40 FOR I = 10 TO 1 STEP -2
50   PRINT I
60 NEXT
```

### DO ... LOOP

```basic
' Condition at start
10 DO WHILE X < 10
20   PRINT X
30   LET X = X + 1
40 LOOP

' Condition at end
50 LET I = 3
60 DO
70   LET I = I - 1
80   PRINT I
90 LOOP UNTIL I = 0
```

### WHILE ... WEND

```basic
10 LET X = 0
20 WHILE X < 3
30   PRINT X
40   LET X = X + 1
50 WEND
```

### Short-Circuit Operators

Use `ANDALSO` and `ORELSE` for short-circuit evaluation:

```basic
10 IF A <> 0 ANDALSO (B / A) > 2 THEN
20   PRINT "Safe division"
30 END IF

40 IF X = 0 ORELSE Y / X > 1 THEN
50   PRINT "Conditional evaluation"
60 END IF
```

> **Note:** `AND` and `OR` always evaluate both operands. Use `ANDALSO` and `ORELSE` to avoid errors like division by zero.

---

## 4. Procedures and Functions

### Subroutines (SUB)

Subroutines are called as statements. Parentheses are recommended and required when passing
arguments. For zero-argument SUBs, the parser also accepts the legacy form without
parentheses in statement position.

```basic
10 SUB GREET(NAME$)
20   PRINT "Hello, "; NAME$
30 END SUB

40 GREET("Ada")          ' Statement call with parentheses
50 GREET                 ' Legacy: zero-arg call without parentheses (statement form)
```

### Functions (FUNCTION)

Functions return values and are used in expressions:

```basic
10 FUNCTION SQUARE(N)
20   RETURN N * N
30 END FUNCTION

40 LET X = SQUARE(9)     ' X = 81
50 PRINT SQUARE(5)       ' 25
```

---

## 5. Objects

Viper BASIC supports lightweight object-oriented programming with classes, methods, constructors, and destructors.

### Basic Class

```basic
10 CLASS Counter
20   X AS INTEGER
30
40   SUB NEW()
50     LET ME.X = 0
60   END SUB
70
80   SUB INCREMENT()
90     LET ME.X = ME.X + 1
100  END SUB
110
120  FUNCTION VALUE()
130    RETURN ME.X
140  END FUNCTION
150 END CLASS

160 DIM C AS Counter
170 LET C = NEW Counter()
180 C.INCREMENT()
190 C.INCREMENT()
200 PRINT C.VALUE()        ' 2
210 DELETE C
```

**Key points:**
- `ME` refers to the current instance (like `this` in C++ or `self` in Python)
- `NEW` is a special constructor method
- `DELETE` frees the object (calls `DESTRUCTOR` if defined)

Common mistakes and tips:
- Missing parentheses when passing arguments will be rejected with a clear diagnostic.
- For readability and consistency, prefer parentheses even for zero-argument calls.

### Destructors

```basic
10 CLASS FileHandler
20   FH AS INTEGER
30
40   SUB NEW(FILENAME$)
50     OPEN FILENAME$ FOR OUTPUT AS #1
60     LET ME.FH = 1
70   END SUB
80
90   DESTRUCTOR()
100    IF ME.FH <> 0 THEN CLOSE #ME.FH
110  END DESTRUCTOR
120 END CLASS
```

**Note:** Destructors are automatically called when an object is deleted or goes out of scope.

---

## 6. Organizing Code with Namespaces

Namespaces help you organize classes and avoid name collisions in larger programs.

### Declaring Namespaces

Use `NAMESPACE` blocks to group related types. Dotted paths create nested namespaces:

```basic
NAMESPACE Graphics.Rendering
  CLASS Renderer
    WIDTH AS I64
    HEIGHT AS I64
  END CLASS
END NAMESPACE

NAMESPACE Graphics.UI
  CLASS Button
    LABEL AS STR
  END CLASS
END NAMESPACE
```

**Fully-qualified names:**

```basic
DIM R AS Graphics.Rendering.Renderer
DIM B AS Graphics.UI.Button
```

### Using the USING Directive

Import types from a namespace for unqualified references:

```basic
USING Graphics.Rendering

DIM R AS Renderer          REM Unqualified via USING
```

**Placement rules:**
- `USING` must appear at file scope
- `USING` must come before `NAMESPACE`, `CLASS`, or `INTERFACE` declarations
- Each file's `USING` directives are file-scoped

### Creating Namespace Aliases

Create shorthand names for long namespace paths:

```basic
USING GR = Graphics.Rendering
USING UI = Graphics.UI

DIM RENDERER AS GR.Renderer
DIM BUTTON AS UI.Button
```

### Type Resolution Precedence

When you reference a type without qualification, the compiler searches in order:

1. Current namespace
2. Parent namespaces (walking up the hierarchy)
3. Imported namespaces via `USING` (in declaration order)
4. Global namespace

### Handling Ambiguity

If multiple imported namespaces contain the same type name:

```basic
USING Collections
USING Utilities

REM If both define "List":
DIM MYLIST AS List          REM Error: ambiguous reference

REM Fix 1: Use fully-qualified name
DIM MYLIST AS Collections.List

REM Fix 2: Use alias
USING Coll = Collections
DIM MYLIST AS Coll.List
```

### Case Insensitivity

All namespace and type names are case-insensitive:

```basic
NAMESPACE MyLib
  CLASS Helper
  END CLASS
END NAMESPACE

REM All equivalent:
DIM H1 AS MyLib.Helper
DIM H2 AS MYLIB.HELPER
DIM H3 AS mylib.helper
```

---

## 7. Console and File I/O

### Console Input/Output

```basic
10 INPUT "What is your name? ", NAME$
20 PRINT "Hello, "; NAME$

' LINE INPUT reads an entire line
30 LINE INPUT "Enter a line: ", LINE$
40 PRINT "You entered: "; LINE$
```

### File Operations

```basic
10 OPEN "output.txt" FOR OUTPUT AS #1
20 PRINT #1, "Hello, file!"
30 PRINT #1, "Line 2"
40 CLOSE #1

50 OPEN "output.txt" FOR INPUT AS #2
60 WHILE NOT EOF(#2)
70   LINE INPUT #2, L$
80   PRINT L$
90 WEND
100 CLOSE #2
```

**File modes:**
- `INPUT` — Read from file
- `OUTPUT` — Write to file (overwrites)
- `APPEND` — Write to file (appends)
- `BINARY` — Binary read/write

---

## 8. Error Handling

### ON ERROR GOTO

```basic
10 ON ERROR GOTO 100
20 OPEN "missing.txt" FOR INPUT AS #1
30 PRINT "File opened successfully"
40 CLOSE #1
50 END

100 REM Error handler
110 PRINT "Error: Could not open file"
120 RESUME 0
```

**Resume options:**
- `RESUME` — Retry the statement that caused the error
- `RESUME NEXT` — Continue with the next statement
- `RESUME <line>` — Jump to a specific line (use `RESUME 0` to end)

---

## 9. Example Projects

### Example A: Number Guessing Game

```basic
10 RANDOMIZE TIMER
20 LET SECRET = INT(RND() * 100) + 1
30 LET ATTEMPTS = 0

40 DO
50   INPUT "Guess a number (1-100): ", GUESS
60   LET ATTEMPTS = ATTEMPTS + 1
70
80   IF GUESS = SECRET THEN
90     PRINT "Correct! You won in "; ATTEMPTS; " attempts!"
100    EXIT DO
110  ELSEIF GUESS < SECRET THEN
120    PRINT "Higher!"
130  ELSE
140    PRINT "Lower!"
150  END IF
160 LOOP
```

### Example B: File Copy Utility

```basic
10 LINE INPUT "Source file: ", SOURCE$
20 LINE INPUT "Destination file: ", DEST$

30 ON ERROR GOTO 200

40 OPEN SOURCE$ FOR INPUT AS #1
50 OPEN DEST$ FOR OUTPUT AS #2

60 WHILE NOT EOF(#1)
70   LINE INPUT #1, LINE$
80   PRINT #2, LINE$
90 WEND

100 CLOSE #1
110 CLOSE #2
120 PRINT "File copied successfully!"
130 END

200 REM Error handler
210 PRINT "Error: Could not copy file"
220 RESUME 0
```

---

## 10. Where to Go Next

**Learn More:**
- **[BASIC Reference](basic-reference.md)** — Complete language reference
- **[IL Guide](il-guide.md)** — Understanding the intermediate language

**Explore Examples:**
- Browse `examples/basic/` for more sample programs
- Check `tests/golden/basic/` for test cases

**Advanced Topics:**
- Object-oriented patterns: builders, resource guards with `DESTRUCTOR`
- Namespace organization strategies for large projects
- Error handling best practices
