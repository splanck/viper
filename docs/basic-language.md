---
status: active
audience: public
last-updated: 2025-10-24
---

# Viper BASIC — Tutorial

This tutorial teaches the Viper BASIC language by example. If you prefer a catalog,
see the **Reference** document.

> **What is Viper BASIC?**  
> A compact, modernised BASIC designed for clarity: `LET` for assignment, clean arrays, short‑circuit booleans,
> lightweight objects, and straightforward console/file I/O. It runs on Viper’s VM and can be lowered to native code.

## 1. First steps: printing and expressions

```basic
10 PRINT "Hello, world!"
20 PRINT 1 + 2 * 3       ' 7
30 PRINT "A"; "B"        ' "AB" (no newline)
```

Try removing line numbers; they are optional. Use `:` to put multiple statements on one line.

## 2. Variables and types

Assignments use **`LET`**. You can declare scalars implicitly by assigning,
or explicitly with `DIM` to pin a type. Arrays require `DIM`.

```basic
10 LET I = 42
20 DIM Flag AS BOOLEAN
30 LET Flag = TRUE
40 DIM A(3)              ' indices 0..2
50 LET A(0) = I
```

Suffixes: `$` string, `#`/`!` float, `&`/`%` integer.

```basic
10 LET S$ = "hi"
20 LET F# = 3.14
```

## 3. Control flow

```basic
10 IF N = 0 THEN
20   PRINT "zero"
30 ELSE
40   PRINT "nonzero"
50 END IF

10 FOR I = 1 TO 5: PRINT I: NEXT

10 LET I = 3
20 DO
30   LET I = I - 1
40 LOOP UNTIL I = 0

10 WHILE X < 3: PRINT X: LET X = X + 1: WEND

' Short-circuit
10 IF A <> 0 ANDALSO (B / A) > 2 THEN PRINT "ok"
```

## 4. Procedures and functions

`SUB` is a procedure (statement call). `FUNCTION` returns a value (use in expressions).

```basic
10 SUB GREET(S$)
20   PRINT "Hello, "; S$
30 END SUB

40 FUNCTION SQUARE(N)
50   RETURN N * N
60 END FUNCTION

70 GREET("Ada")          ' statement call; parentheses required
80 LET X = SQUARE(9)     ' expression call
```

## 5. Objects

```basic
10 CLASS Counter
20   X AS INTEGER
30   SUB NEW(): LET ME.X = 0: END SUB
40   SUB INC(): LET ME.X = ME.X + 1: END SUB
50   FUNCTION Current(): RETURN ME.X: END FUNCTION
60 END CLASS

70 DIM c AS Counter
80 LET c = NEW Counter()
90 c.INC()
100 PRINT c.Current()
110 DELETE c
```

## 6. Console and file I/O

```basic
10 INPUT "Name? ", N$
20 PRINT "Hello, "; N$

10 OPEN "out.txt" FOR OUTPUT AS #1
20 PRINT #1, "Hello"
30 CLOSE #1
```

## 7. Errors

```basic
10 ON ERROR GOTO 100
20 OPEN "missing.txt" FOR INPUT AS #1
30 END
100 PRINT "Could not open file"
110 RESUME 0
```

## 8. Mini-project A: number guess

```basic
10 LET SECRET = 42
20 DO
30   INPUT "Guess? ", G
40   IF G = SECRET THEN PRINT "Correct!": EXIT DO
50   IF G < SECRET THEN PRINT "Higher" ELSE PRINT "Lower"
60 LOOP
```

## 9. Mini-project B: file copy (lines)

```basic
10 LINE INPUT "Source? ", S$
20 LINE INPUT "Dest? ", D$
30 OPEN S$ FOR INPUT AS #1
40 OPEN D$ FOR OUTPUT AS #2
50 WHILE NOT EOF(#1)
60   LINE INPUT #1, L$
70   PRINT #2, L$
80 WEND
90 CLOSE #1: CLOSE #2
```

## 10. Where to go next

- Skim the **Reference** for all statements and built-ins.
- Review examples in `tests/golden/basic/` (if available).
- Explore OOP patterns: builders, resource guards with `DESTRUCTOR`, and collection utilities.
