---
status: active
audience: public
last-updated: 2025-10-24
---

# Viper BASIC — Reference

This is the canonical reference for the Viper BASIC language. It describes **statements (commands)**,
**expressions & operators**, **built-in functions**, **object features**, and **I/O**. Every statement includes
a brief description and a minimal example.

> **Key dialect facts**
> • Assignment requires **`LET`** (e.g., `LET X = 2`). A bare `X = 2` is not a statement.
> • There is **no `CALL` keyword**. Statement calls use `Name(args)` with parentheses.
> • **Built-ins must be called with parentheses.** Zero-argument built-ins use empty parentheses, e.g., `INKEY$()`, `GETKEY$()`, `RND()`.
> • Arrays are 1‑D, zero‑based; `DIM`/`REDIM` are required for arrays.
> • Booleans short‑circuit: `ANDALSO` / `ORELSE`.
> • Functions return with `RETURN`; SUBs are called as statements.
> • OOP: `CLASS`, methods, fields, `ME`, `NEW`, `DELETE`, optional `DESTRUCTOR`.

## Statements A–Z

### '

Comment. A leading apostrophe starts a comment.

```basic
10 ' Single-line comment
20 PRINT "Hello"
```

### CLASS / NEW / DELETE

Defines classes; construct with NEW, optionally free with DELETE.

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

Sets terminal foreground and background colors. Uses values 0-7 for normal colors, 8-15 for bright colors, or -1 to leave unchanged.

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
| ------- | ---- | ------- |
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
