<!--
SPDX-License-Identifier: MIT
File: docs/references/basic.md
Purpose: Reference for Viper BASIC v0.1 features.
-->

# BASIC Reference

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
10 LET N = 5
20 LET PI# = 3.14159
30 LET MSG$ = "OK"
```

## Expressions
Arithmetic operators: `+`, `-`, `*`, `/`, integer division `\`, `MOD`.
Relational operators: `=`, `<>`, `<`, `<=`, `>`, `>=`.
Logical operators: `AND`, `OR`, `NOT`.
Operator precedence: arithmetic > relational > logical.

```basic
10 LET A = 5 + 2 * 3
20 LET B = 7 \ 2
30 LET R = 7 MOD 2
40 IF A >= 11 AND NOT B = 3 THEN PRINT R
```

## PRINT
`PRINT` outputs expressions. Commas insert a space; semicolons suppress spacing. A trailing semicolon suppresses the newline.

```basic
10 PRINT 42
20 PRINT "A", 1
30 PRINT "B";2
40 PRINT "No newline";
```

## INPUT
Reads a value from standard input. Strings require quotes in prompts.

```basic
10 INPUT N
20 INPUT "Name? ", S$
30 PRINT S$, N
```

## Control Flow
### IF / ELSEIF / ELSE / END IF
```basic
10 INPUT N
20 IF N < 0 THEN
30   PRINT "neg"
40 ELSEIF N = 0 THEN
50   PRINT "zero"
60 ELSE
70   PRINT "pos"
80 END IF
```

### WHILE / WEND
```basic
10 LET I = 0
20 WHILE I < 3
30   PRINT I
40   LET I = I + 1
50 WEND
```

### FOR / NEXT
```basic
10 FOR I = 1 TO 3
20   PRINT I
30 NEXT I
40 FOR J = 5 TO 1 STEP -2
50   PRINT J
60 NEXT J
```

### Multi-statement lines
Separate multiple statements on one line with `:`.

```basic
10 LET A = 1: LET B = 2: PRINT A + B
```

## Procedures
### FUNCTION
```basic
10 FUNCTION Fact(N)
20   IF N <= 1 THEN RETURN 1
30   RETURN N * Fact(N - 1)
40 END FUNCTION
50 PRINT Fact(5)
```

### SUB
```basic
10 SUB Hello(Name$)
20   PRINT "Hello "; Name$
30 END SUB
40 Hello "Ada"
```

### Parameters
Scalar parameters are passed by value. Arrays and strings are passed by reference.

```basic
10 SUB Inc(N)
20   LET N = N + 1
30 END SUB
40 LET X = 1
50 Inc X
60 PRINT X        ' still 1

70 SUB SetName(S$)
80   LET S$ = "Hi"
90 END SUB
100 LET T$ = ""
110 SetName T$
120 PRINT T$      ' prints Hi

130 SUB Fill(A())
140   LET A(0) = 1
150 END SUB
160 DIM B(1)
170 Fill B
180 PRINT B(0)    ' prints 1
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

## Math and Built-in Functions
```basic
10 RANDOMIZE 0
20 PRINT LEN("abc")
30 PRINT MID$("hello",2,2)
40 PRINT LEFT$("hello",3)
50 PRINT RIGHT$("hello",2)
60 PRINT ABS(-3), ABS(-3#)
70 PRINT SQR(9), FLOOR(2.7), CEIL(2.1), POW(2,3)
80 PRINT SIN(0), COS(0)
90 PRINT RND()
```

## Debugging Options
Use command-line flags with the compiler:

```bash
ilc --trace=src program.bas
ilc --break main.bas:10 --watch X program.bas
```
