# Appendix B: BASIC Reference

A quick reference for Viper BASIC syntax and features.

---

## Comments

```basic
' Single-line comment
REM This is also a comment
```

---

## Variables

```basic
DIM x AS INTEGER          ' Declare with type
DIM y AS INTEGER = 42     ' Declare and initialize
DIM z = 42                ' Type inferred

CONST PI = 3.14159        ' Constant
```

---

## Data Types

| Type | Description | Example |
|------|-------------|---------|
| `INTEGER` | 64-bit signed integer | `42`, `-7` |
| `LONG` | Alias for INTEGER | `42` |
| `SINGLE` | 32-bit float | `3.14` |
| `DOUBLE` | 64-bit float | `3.14159265` |
| `BOOLEAN` | True/False | `TRUE`, `FALSE` |
| `STRING` | Text | `"hello"` |
| `BYTE` | 8-bit unsigned | `255` |

---

## Operators

### Arithmetic
```basic
a + b    ' Addition
a - b    ' Subtraction
a * b    ' Multiplication
a / b    ' Division
a MOD b  ' Modulo
a \ b    ' Integer division
a ^ b    ' Exponentiation
-a       ' Negation
```

### Comparison
```basic
a = b    ' Equal
a <> b   ' Not equal
a < b    ' Less than
a <= b   ' Less than or equal
a > b    ' Greater than
a >= b   ' Greater than or equal
```

### Logical
```basic
a AND b  ' Logical and
a OR b   ' Logical or
NOT a    ' Logical not
a XOR b  ' Exclusive or
```

### String
```basic
a & b    ' Concatenation
a + b    ' Also concatenation
```

---

## Control Flow

### IF/THEN/ELSE
```basic
IF condition THEN
    ' ...
ELSEIF other THEN
    ' ...
ELSE
    ' ...
END IF

' Single line form
IF x > 0 THEN PRINT "positive"
```

### SELECT CASE
```basic
SELECT CASE value
    CASE 1
        ' handle 1
    CASE 2, 3
        ' handle 2 or 3
    CASE 4 TO 10
        ' handle 4 through 10
    CASE IS > 100
        ' handle > 100
    CASE ELSE
        ' default
END SELECT
```

### FOR/NEXT
```basic
FOR i = 1 TO 10
    PRINT i
NEXT i

FOR i = 10 TO 1 STEP -1
    PRINT i
NEXT i

FOR i = 0 TO 100 STEP 10
    PRINT i
NEXT i
```

### FOR EACH
```basic
FOR EACH item IN collection
    PRINT item
NEXT item
```

### WHILE/WEND
```basic
WHILE condition
    ' ...
WEND
```

### DO/LOOP
```basic
DO WHILE condition
    ' ...
LOOP

DO
    ' ...
LOOP WHILE condition

DO UNTIL condition
    ' ...
LOOP

DO
    ' ...
LOOP UNTIL condition
```

### EXIT and CONTINUE
```basic
FOR i = 1 TO 100
    IF i = 50 THEN EXIT FOR
    IF i MOD 2 = 0 THEN CONTINUE FOR
    PRINT i
NEXT i
```

---

## Subroutines and Functions

### Subroutines
```basic
SUB Greet(name AS STRING)
    PRINT "Hello, " & name
END SUB

' Call subroutine
CALL Greet("Alice")
Greet "Alice"         ' CALL is optional
```

### Functions
```basic
FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
    Add = a + b       ' Assign to function name to return
END FUNCTION

FUNCTION Multiply(a AS INTEGER, b AS INTEGER) AS INTEGER
    RETURN a * b      ' Alternative return syntax
END FUNCTION

' Call function
DIM result AS INTEGER
result = Add(3, 4)
```

### Optional Parameters
```basic
SUB Greet(name AS STRING, OPTIONAL greeting AS STRING = "Hello")
    PRINT greeting & ", " & name
END SUB

Greet "Alice"              ' Uses default
Greet "Bob", "Hi"          ' Uses provided value
```

---

## Arrays

```basic
DIM numbers(10) AS INTEGER           ' Array of 11 elements (0-10)
DIM matrix(5, 5) AS INTEGER          ' 2D array

numbers(0) = 42                      ' Set element
DIM x AS INTEGER = numbers(0)        ' Get element

REDIM numbers(20)                    ' Resize array
REDIM PRESERVE numbers(30)           ' Resize keeping data

' Array functions
DIM length AS INTEGER = UBOUND(numbers)
DIM lower AS INTEGER = LBOUND(numbers)
```

### Dynamic Arrays
```basic
DIM items() AS STRING                ' Dynamic array
REDIM items(5)
items(0) = "first"

' Add element
REDIM PRESERVE items(UBOUND(items) + 1)
items(UBOUND(items)) = "new item"
```

---

## Strings

```basic
DIM s AS STRING = "Hello, World!"

' Functions
DIM length AS INTEGER = LEN(s)
DIM upper AS STRING = UCASE(s)
DIM lower AS STRING = LCASE(s)
DIM trimmed AS STRING = TRIM(s)
DIM left5 AS STRING = LEFT(s, 5)
DIM right5 AS STRING = RIGHT(s, 5)
DIM middle AS STRING = MID(s, 4, 5)
DIM pos AS INTEGER = INSTR(s, "World")
DIM replaced AS STRING = REPLACE(s, "World", "BASIC")

' String concatenation
DIM greeting AS STRING = "Hello" & ", " & "World"
```

---

## Input/Output

### Console
```basic
PRINT "Hello, World!"
PRINT "Value: "; x
PRINT "A", "B", "C"           ' Tab-separated

INPUT "Enter name: ", name$
INPUT x                        ' Numeric input

LINE INPUT prompt$, response$  ' Read entire line
```

### Files
```basic
' Write to file
OPEN "data.txt" FOR OUTPUT AS #1
PRINT #1, "Line 1"
PRINT #1, "Line 2"
CLOSE #1

' Read from file
OPEN "data.txt" FOR INPUT AS #1
WHILE NOT EOF(1)
    LINE INPUT #1, line$
    PRINT line$
WEND
CLOSE #1

' Append to file
OPEN "data.txt" FOR APPEND AS #1
PRINT #1, "New line"
CLOSE #1
```

---

## User-Defined Types (Structures)

```basic
TYPE Point
    x AS DOUBLE
    y AS DOUBLE
END TYPE

DIM p AS Point
p.x = 10.0
p.y = 20.0

TYPE Person
    name AS STRING
    age AS INTEGER

    SUB Birthday()
        THIS.age = THIS.age + 1
    END SUB
END TYPE
```

---

## Classes

```basic
CLASS Counter
    PRIVATE count AS INTEGER

    SUB New()
        count = 0
    END SUB

    SUB New(initial AS INTEGER)
        count = initial
    END SUB

    SUB Increment()
        count = count + 1
    END SUB

    FUNCTION GetCount() AS INTEGER
        GetCount = count
    END FUNCTION
END CLASS

DIM c AS Counter = NEW Counter()
c.Increment()
PRINT c.GetCount()
```

### Inheritance
```basic
CLASS Animal
    PROTECTED name AS STRING

    SUB New(n AS STRING)
        name = n
    END SUB

    VIRTUAL SUB Speak()
        PRINT "..."
    END SUB
END CLASS

CLASS Dog INHERITS Animal
    SUB New(n AS STRING)
        SUPER.New(n)
    END SUB

    OVERRIDE SUB Speak()
        PRINT name & " says Woof!"
    END SUB
END CLASS
```

---

## Interfaces

```basic
INTERFACE Drawable
    SUB Draw()
    FUNCTION GetBounds() AS Rect
END INTERFACE

CLASS Circle IMPLEMENTS Drawable
    SUB Draw()
        ' ...
    END SUB

    FUNCTION GetBounds() AS Rect
        ' ...
    END FUNCTION
END CLASS
```

---

## Error Handling

```basic
ON ERROR GOTO ErrorHandler
' risky code here
ON ERROR GOTO 0              ' Turn off error handling

ErrorHandler:
    PRINT "Error: " & ERR.Description
    RESUME NEXT              ' Continue after error

' Modern try/catch style
TRY
    ' risky code
CATCH ex AS Exception
    PRINT "Error: " & ex.Message
FINALLY
    ' cleanup
END TRY
```

---

## Built-in Functions

### Math
```basic
ABS(x)           ' Absolute value
SGN(x)           ' Sign (-1, 0, 1)
INT(x)           ' Integer part
FIX(x)           ' Truncate toward zero
ROUND(x)         ' Round to nearest
ROUND(x, n)      ' Round to n decimal places
SQR(x)           ' Square root
LOG(x)           ' Natural logarithm
EXP(x)           ' e^x
SIN(x)           ' Sine
COS(x)           ' Cosine
TAN(x)           ' Tangent
ATN(x)           ' Arctangent
RND()            ' Random 0-1
RANDOMIZE        ' Seed random generator
MIN(a, b)        ' Minimum
MAX(a, b)        ' Maximum
```

### String
```basic
LEN(s$)          ' Length
UCASE$(s$)       ' Uppercase
LCASE$(s$)       ' Lowercase
TRIM$(s$)        ' Trim whitespace
LTRIM$(s$)       ' Trim left
RTRIM$(s$)       ' Trim right
LEFT$(s$, n)     ' Left n characters
RIGHT$(s$, n)    ' Right n characters
MID$(s$, start, len)  ' Substring
INSTR(s$, find$) ' Find position
REPLACE$(s$, old$, new$)  ' Replace
SPLIT(s$, delim$) ' Split to array
ASC(c$)          ' Character code
CHR$(n)          ' Code to character
STR$(n)          ' Number to string
VAL(s$)          ' String to number
```

### Conversion
```basic
CINT(x)          ' Convert to integer
CLNG(x)          ' Convert to long
CSNG(x)          ' Convert to single
CDBL(x)          ' Convert to double
CSTR(x)          ' Convert to string
CBOOL(x)         ' Convert to boolean
```

### Date/Time
```basic
NOW              ' Current date/time
DATE$            ' Current date string
TIME$            ' Current time string
TIMER            ' Seconds since midnight
YEAR(d)          ' Year from date
MONTH(d)         ' Month from date
DAY(d)           ' Day from date
HOUR(t)          ' Hour from time
MINUTE(t)        ' Minute from time
SECOND(t)        ' Second from time
```

---

## Modules

```basic
MODULE MyModule

PUBLIC SUB PublicSub()
    ' ...
END SUB

PRIVATE SUB PrivateSub()
    ' ...
END SUB

END MODULE

' In another file
IMPORT MyModule

CALL MyModule.PublicSub()
```

---

## Special Syntax

### Line Continuation
```basic
DIM longLine AS STRING = "This is a very " & _
    "long string that " & _
    "continues on multiple lines"
```

### Multiple Statements
```basic
x = 1 : y = 2 : z = 3    ' Multiple statements on one line
```

### Type Suffixes
```basic
DIM x% = 42       ' Integer
DIM y& = 42       ' Long
DIM z! = 3.14     ' Single
DIM w# = 3.14159  ' Double
DIM s$ = "hello"  ' String
```

---

## Keywords

```
AND         AS          BOOLEAN     BYTE        BYREF
BYVAL       CALL        CASE        CATCH       CLASS
CLOSE       CONST       CONTINUE    DATE        DECLARE
DIM         DO          DOUBLE      EACH        ELSE
ELSEIF      END         EOF         ERASE       ERROR
EXIT        FALSE       FINALLY     FOR         FUNCTION
GET         GLOBAL      GOSUB       GOTO        IF
IMPLEMENTS  IMPORT      IN          INHERITS    INPUT
INTEGER     INTERFACE   IS          LET         LINE
LOCAL       LONG        LOOP        MOD         MODULE
NEW         NEXT        NOT         NOTHING     ON
OPEN        OPTION      OPTIONAL    OR          OUTPUT
OVERRIDE    PRESERVE    PRINT       PRIVATE     PROPERTY
PROTECTED   PUBLIC      PUT         RANDOMIZE   REDIM
REM         RESUME      RETURN      SELECT      SET
SHARED      SINGLE      STATIC      STEP        STOP
STRING      SUB         SUPER       THEN        THIS
THROW       TO          TRUE        TRY         TYPE
UNTIL       VARIANT     VIRTUAL     WEND        WHILE
WITH        WRITE       XOR
```

---

*[Back to Table of Contents](../README.md) | [Prev: Appendix A](a-viperlang-reference.md) | [Next: Appendix C: Pascal Reference →](c-pascal-reference.md)*
