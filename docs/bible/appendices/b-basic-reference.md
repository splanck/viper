# Appendix B: Viper BASIC Reference

A comprehensive reference for Viper BASIC syntax and features. This appendix serves as a quick lookup for all BASIC language constructs, with examples, explanations, and cross-references to the chapters where each concept is taught in depth.

---

## Table of Contents

1. [Comments](#comments)
2. [Variables and Constants](#variables-and-constants)
3. [Data Types](#data-types)
4. [Operators](#operators)
5. [Control Flow](#control-flow)
6. [Subroutines and Functions](#subroutines-and-functions)
7. [Arrays](#arrays)
8. [Strings](#strings)
9. [Input/Output](#inputoutput)
10. [User-Defined Types](#user-defined-types-structures)
11. [Classes and Objects](#classes-and-objects)
12. [Interfaces](#interfaces)
13. [Error Handling](#error-handling)
14. [Built-in Functions](#built-in-functions)
15. [Modules](#modules)
16. [Special Syntax](#special-syntax)
17. [Common Patterns](#common-patterns)
18. [Keywords Reference](#keywords-reference)

---

## Comments

Comments document your code and are ignored by the compiler. Use them liberally to explain *why* code does what it does, not just *what* it does.

> **See also:** [Chapter 2: Your First Program](../part1-foundations/02-first-program.md) for introduction to comments.

### Single-Line Comments

```basic
' This is a single-line comment (apostrophe style)
REM This is also a single-line comment (traditional BASIC style)

DIM x AS INTEGER = 42  ' Comments can appear at end of lines
```

### When to Use Each Style

| Style | Best For |
|-------|----------|
| `'` (apostrophe) | Quick inline comments, end-of-line notes |
| `REM` | Prominent section headers, compatibility with classic BASIC |

### Common Pattern: Section Headers

```basic
REM ============================================
REM INITIALIZATION SECTION
REM ============================================

DIM score AS INTEGER = 0
DIM lives AS INTEGER = 3

' Player starting position
DIM playerX AS INTEGER = 100
DIM playerY AS INTEGER = 200
```

**Zia equivalent:**
```rust
// Single-line comment
/* Multi-line comment */
/// Documentation comment
```

---

## Variables and Constants

Variables are named storage locations that hold values. Constants are variables whose values cannot change after initialization.

> **See also:** [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md) for a complete explanation of variables, types, and naming.

### Declaring Variables

```basic
' Explicit type declaration
DIM count AS INTEGER

' Declaration with initialization
DIM score AS INTEGER = 0

' Type inference (compiler determines type from value)
DIM message = "Hello"      ' Inferred as STRING
DIM pi = 3.14159           ' Inferred as DOUBLE

' Multiple declarations
DIM x AS INTEGER, y AS INTEGER, z AS INTEGER
DIM a AS INTEGER = 1, b AS INTEGER = 2
```

### Constants

Constants prevent accidental modification of values that should never change:

```basic
CONST MAX_PLAYERS = 4
CONST PI = 3.14159265359
CONST GAME_TITLE = "Space Invaders"
CONST DEBUG_MODE = TRUE

' Using constants
DIM players(MAX_PLAYERS) AS Player
DIM circumference AS DOUBLE = 2 * PI * radius
```

### Variable Naming Conventions

| Convention | Example | Use For |
|------------|---------|---------|
| camelCase | `playerScore` | Local variables, parameters |
| PascalCase | `PlayerName` | Subroutines, functions, types |
| UPPER_CASE | `MAX_HEALTH` | Constants |
| Type suffix | `name$`, `count%` | Classic BASIC compatibility |

### Scope and Lifetime

```basic
' Module-level variable (accessible throughout module)
DIM globalCounter AS INTEGER = 0

SUB ProcessItems()
    ' Local variable (only accessible within this subroutine)
    DIM localCount AS INTEGER = 0

    ' Static variable (retains value between calls)
    STATIC callCount AS INTEGER = 0
    callCount = callCount + 1
END SUB
```

**Zia equivalent:**
```rust
var count = 0;        // Mutable variable
final PI = 3.14159;   // Immutable constant
```

---

## Data Types

Every value in BASIC has a type that determines what operations can be performed on it and how much memory it uses.

> **See also:** [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md) for understanding types conceptually.

### Primitive Types

| Type | Size | Range | Example | Use Case |
|------|------|-------|---------|----------|
| `BYTE` | 8-bit | 0 to 255 | `255` | Pixel colors, small counters |
| `INTEGER` | 64-bit | -9.2 quintillion to 9.2 quintillion | `42`, `-7` | General-purpose whole numbers |
| `LONG` | 64-bit | Same as INTEGER | `42` | Alias for INTEGER (compatibility) |
| `SINGLE` | 32-bit | ~7 significant digits | `3.14` | Graphics coordinates, fast math |
| `DOUBLE` | 64-bit | ~15 significant digits | `3.14159265` | Scientific calculations, precision |
| `BOOLEAN` | 1-bit | `TRUE` or `FALSE` | `TRUE` | Flags, conditions |
| `STRING` | Variable | Any text | `"hello"` | Text, messages, names |

### Choosing the Right Type

```basic
' Use INTEGER for counting and indexing
DIM itemCount AS INTEGER = 0
DIM arrayIndex AS INTEGER = 5

' Use DOUBLE for calculations requiring precision
DIM bankBalance AS DOUBLE = 1234.56
DIM scientificValue AS DOUBLE = 6.02214076E23

' Use SINGLE for graphics (faster, less memory)
DIM screenX AS SINGLE = 320.5
DIM screenY AS SINGLE = 240.0

' Use BOOLEAN for yes/no states
DIM isGameOver AS BOOLEAN = FALSE
DIM hasKey AS BOOLEAN = TRUE

' Use BYTE for memory-efficient small values
DIM red AS BYTE = 255
DIM green AS BYTE = 128
DIM blue AS BYTE = 0

' Use STRING for text
DIM playerName AS STRING = "Alice"
DIM welcomeMessage AS STRING = "Press START to begin"
```

### Type Suffixes (Classic BASIC Style)

For compatibility with classic BASIC, you can use type suffixes instead of AS clauses:

```basic
DIM x% = 42         ' INTEGER
DIM y& = 42         ' LONG
DIM z! = 3.14       ' SINGLE
DIM w# = 3.14159    ' DOUBLE
DIM s$ = "hello"    ' STRING
```

### Literal Formats

```basic
' Integer literals
DIM decimal AS INTEGER = 255
DIM hex AS INTEGER = &HFF        ' Hexadecimal (255)
DIM binary AS INTEGER = &B11111111  ' Binary (255)
DIM octal AS INTEGER = &O377     ' Octal (255)

' Floating-point literals
DIM normal AS DOUBLE = 3.14159
DIM scientific AS DOUBLE = 6.022E23    ' Scientific notation
DIM negative AS DOUBLE = -273.15

' String literals
DIM simple AS STRING = "Hello"
DIM withQuotes AS STRING = "She said ""Hello"""  ' Escaped quotes
DIM empty AS STRING = ""

' Boolean literals
DIM yes AS BOOLEAN = TRUE
DIM no AS BOOLEAN = FALSE
```

**Zia equivalent:**
```rust
var count: i64 = 42;      // Explicit type
var price: f64 = 19.99;   // 64-bit float
var name: string = "Bob"; // String
var active: bool = true;  // Boolean
```

---

## Operators

Operators perform operations on values. Understanding operator precedence is crucial for writing correct expressions.

> **See also:** [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md) for basic operators, [Chapter 4: Making Decisions](../part1-foundations/04-decisions.md) for comparison and logical operators.

### Arithmetic Operators

```basic
DIM a AS INTEGER = 10
DIM b AS INTEGER = 3

PRINT a + b      ' Addition: 13
PRINT a - b      ' Subtraction: 7
PRINT a * b      ' Multiplication: 30
PRINT a / b      ' Division: 3.333... (always returns DOUBLE)
PRINT a \ b      ' Integer division: 3 (truncates toward zero)
PRINT a MOD b    ' Modulo (remainder): 1
PRINT a ^ b      ' Exponentiation: 1000 (10 to the power of 3)
PRINT -a         ' Negation: -10
```

### Practical Examples

```basic
' Check if a number is even or odd
DIM number AS INTEGER = 42
IF number MOD 2 = 0 THEN
    PRINT "Even"
ELSE
    PRINT "Odd"
END IF

' Convert total seconds to hours, minutes, seconds
DIM totalSeconds AS INTEGER = 3725
DIM hours AS INTEGER = totalSeconds \ 3600
DIM minutes AS INTEGER = (totalSeconds MOD 3600) \ 60
DIM seconds AS INTEGER = totalSeconds MOD 60
PRINT hours; ":"; minutes; ":"; seconds  ' 1:2:5

' Calculate compound interest
DIM principal AS DOUBLE = 1000.0
DIM rate AS DOUBLE = 0.05
DIM years AS INTEGER = 10
DIM amount AS DOUBLE = principal * (1 + rate) ^ years
```

### Comparison Operators

```basic
DIM x AS INTEGER = 5
DIM y AS INTEGER = 10

PRINT x = y      ' Equal: FALSE
PRINT x <> y     ' Not equal: TRUE
PRINT x < y      ' Less than: TRUE
PRINT x <= y     ' Less than or equal: TRUE
PRINT x > y      ' Greater than: FALSE
PRINT x >= y     ' Greater than or equal: FALSE
```

### String Comparison

```basic
DIM name1 AS STRING = "Alice"
DIM name2 AS STRING = "Bob"

IF name1 = name2 THEN PRINT "Same name"
IF name1 < name2 THEN PRINT "Alice comes before Bob"  ' Alphabetical

' Case-sensitive comparison
IF UCASE(name1) = UCASE(name2) THEN PRINT "Same (ignoring case)"
```

### Logical Operators

```basic
DIM a AS BOOLEAN = TRUE
DIM b AS BOOLEAN = FALSE

PRINT a AND b    ' Logical AND: FALSE (both must be true)
PRINT a OR b     ' Logical OR: TRUE (at least one true)
PRINT NOT a      ' Logical NOT: FALSE (inverts)
PRINT a XOR b    ' Exclusive OR: TRUE (exactly one true)

' Practical example: game logic
DIM hasKey AS BOOLEAN = TRUE
DIM doorUnlocked AS BOOLEAN = FALSE

IF hasKey AND NOT doorUnlocked THEN
    PRINT "Use key to unlock door"
END IF

' Combining conditions
DIM age AS INTEGER = 25
DIM hasLicense AS BOOLEAN = TRUE

IF age >= 18 AND hasLicense THEN
    PRINT "Can drive"
END IF
```

### String Operators

```basic
' Concatenation
DIM firstName AS STRING = "John"
DIM lastName AS STRING = "Doe"

DIM fullName AS STRING = firstName & " " & lastName  ' "John Doe"
DIM also AS STRING = firstName + " " + lastName      ' Also works

' Building messages
DIM score AS INTEGER = 100
PRINT "Your score: " & STR(score) & " points"
```

### Operator Precedence (Highest to Lowest)

| Precedence | Operators | Description |
|------------|-----------|-------------|
| 1 | `()` | Parentheses (grouping) |
| 2 | `^` | Exponentiation |
| 3 | `-` (unary) | Negation |
| 4 | `*`, `/`, `\`, `MOD` | Multiplication, division |
| 5 | `+`, `-` | Addition, subtraction |
| 6 | `&` | String concatenation |
| 7 | `=`, `<>`, `<`, `<=`, `>`, `>=` | Comparison |
| 8 | `NOT` | Logical NOT |
| 9 | `AND` | Logical AND |
| 10 | `OR`, `XOR` | Logical OR, XOR |

```basic
' Precedence examples
DIM result AS INTEGER

result = 2 + 3 * 4        ' 14 (multiplication first)
result = (2 + 3) * 4      ' 20 (parentheses override)
result = 2 ^ 3 ^ 2        ' 512 (right-to-left for exponents)

' Logical precedence
DIM x AS BOOLEAN
x = TRUE OR FALSE AND FALSE   ' TRUE (AND evaluated first)
x = (TRUE OR FALSE) AND FALSE ' FALSE (parentheses override)
```

**Zia equivalent:**
```rust
a + b    // Addition
a == b   // Equal (note: double equals)
a != b   // Not equal
a && b   // Logical AND
a || b   // Logical OR
!a       // Logical NOT
```

---

## Control Flow

Control flow statements determine which code executes and in what order.

> **See also:** [Chapter 4: Making Decisions](../part1-foundations/04-decisions.md) for conditionals, [Chapter 5: Repetition](../part1-foundations/05-repetition.md) for loops.

### IF/THEN/ELSE

The most fundamental decision-making construct:

```basic
' Simple IF
IF score > 100 THEN
    PRINT "High score!"
END IF

' IF with ELSE
IF temperature > 30 THEN
    PRINT "It's hot!"
ELSE
    PRINT "It's comfortable"
END IF

' Multiple conditions with ELSEIF
IF grade >= 90 THEN
    PRINT "A"
ELSEIF grade >= 80 THEN
    PRINT "B"
ELSEIF grade >= 70 THEN
    PRINT "C"
ELSEIF grade >= 60 THEN
    PRINT "D"
ELSE
    PRINT "F"
END IF

' Single-line form (for simple cases)
IF x > 0 THEN PRINT "positive"
IF x > 0 THEN PRINT "positive" ELSE PRINT "not positive"
```

### Nested IF Statements

```basic
IF hasAccount THEN
    IF isVerified THEN
        IF balance > 0 THEN
            PRINT "Ready to purchase"
        ELSE
            PRINT "Insufficient funds"
        END IF
    ELSE
        PRINT "Please verify your account"
    END IF
ELSE
    PRINT "Please create an account"
END IF
```

### SELECT CASE

Use SELECT CASE when comparing one value against multiple possibilities:

```basic
' Basic SELECT CASE
SELECT CASE dayNumber
    CASE 1
        PRINT "Monday"
    CASE 2
        PRINT "Tuesday"
    CASE 3
        PRINT "Wednesday"
    CASE 4
        PRINT "Thursday"
    CASE 5
        PRINT "Friday"
    CASE 6, 7
        PRINT "Weekend!"
    CASE ELSE
        PRINT "Invalid day"
END SELECT

' Range matching
SELECT CASE score
    CASE 0
        PRINT "No points yet"
    CASE 1 TO 10
        PRINT "Getting started"
    CASE 11 TO 50
        PRINT "Making progress"
    CASE 51 TO 100
        PRINT "Doing well!"
    CASE IS > 100
        PRINT "Expert level!"
    CASE ELSE
        PRINT "Invalid score"
END SELECT

' String matching
SELECT CASE UCASE(command)
    CASE "QUIT", "EXIT", "Q"
        EndProgram()
    CASE "HELP", "?"
        ShowHelp()
    CASE "SAVE"
        SaveGame()
    CASE ELSE
        PRINT "Unknown command"
END SELECT

' Multiple conditions combined
SELECT CASE grade
    CASE "A", "A+", "A-"
        PRINT "Excellent!"
    CASE "B" TO "B+"
        PRINT "Good job"
    CASE IS >= "C"
        PRINT "Passing"
    CASE ELSE
        PRINT "Needs improvement"
END SELECT
```

### FOR/NEXT Loops

Use FOR when you know how many times to repeat:

```basic
' Count from 1 to 10
FOR i = 1 TO 10
    PRINT i
NEXT i

' Count backwards
FOR i = 10 TO 1 STEP -1
    PRINT i
NEXT i
PRINT "Liftoff!"

' Skip values (count by 2)
FOR i = 0 TO 100 STEP 2
    PRINT i   ' 0, 2, 4, 6, ... 100
NEXT i

' Fractional steps
FOR angle = 0 TO 360 STEP 0.5
    DIM radians AS DOUBLE = angle * PI / 180
    PRINT SIN(radians)
NEXT angle

' The NEXT variable is optional (but recommended for clarity)
FOR i = 1 TO 5
    PRINT i
NEXT       ' Works, but less clear
```

### FOR EACH Loops

Use FOR EACH to iterate over collections:

```basic
' Iterate over array
DIM names() AS STRING = {"Alice", "Bob", "Carol"}
FOR EACH name IN names
    PRINT "Hello, " & name
NEXT name

' Iterate over characters in string
DIM word AS STRING = "BASIC"
FOR EACH letter IN word
    PRINT letter
NEXT letter
```

### WHILE/WEND Loops

Use WHILE when you don't know how many iterations in advance:

```basic
' Read until end of file
WHILE NOT EOF(1)
    LINE INPUT #1, line$
    PRINT line$
WEND

' Game loop
DIM playing AS BOOLEAN = TRUE
WHILE playing
    ProcessInput()
    UpdateGame()
    DrawScreen()
WEND

' Input validation
DIM password AS STRING = ""
WHILE LEN(password) < 8
    INPUT "Enter password (8+ chars): ", password
WEND
```

### DO/LOOP Variations

DO/LOOP provides more flexibility than WHILE/WEND:

```basic
' Test condition at start (same as WHILE)
DO WHILE lives > 0
    PlayRound()
LOOP

' Test condition at end (always executes at least once)
DO
    INPUT "Enter a number (1-10): ", n
LOOP WHILE n < 1 OR n > 10

' UNTIL is opposite of WHILE
DO UNTIL gameOver
    ProcessFrame()
LOOP

DO
    INPUT "Continue? (Y/N): ", answer$
LOOP UNTIL UCASE(answer$) = "N"
```

### Choosing the Right Loop

| Situation | Best Loop | Example |
|-----------|-----------|---------|
| Known number of iterations | `FOR` | Process items 1 through 100 |
| Process each item in collection | `FOR EACH` | Print each name in list |
| Unknown iterations, test first | `WHILE` or `DO WHILE` | Read until end of file |
| Unknown iterations, execute at least once | `DO...LOOP UNTIL` | Get valid user input |
| Count down or use custom step | `FOR...STEP` | Countdown, skip every other |

### EXIT and CONTINUE

Control loop execution flow:

```basic
' EXIT breaks out of the loop entirely
FOR i = 1 TO 1000
    IF data(i) = searchValue THEN
        PRINT "Found at index "; i
        EXIT FOR   ' Stop searching
    END IF
NEXT i

' CONTINUE skips to the next iteration
FOR i = 1 TO 100
    IF i MOD 2 = 0 THEN CONTINUE FOR  ' Skip even numbers
    PRINT i   ' Only prints odd numbers
NEXT i

' Works with all loop types
DO WHILE TRUE
    INPUT "Enter command: ", cmd$
    IF cmd$ = "" THEN CONTINUE DO     ' Ignore empty input
    IF cmd$ = "quit" THEN EXIT DO     ' Exit the loop
    ProcessCommand(cmd$)
LOOP

WHILE condition
    IF shouldSkip THEN CONTINUE WHILE
    IF shouldStop THEN EXIT WHILE
    ' ... process ...
WEND
```

**Zia equivalent:**
```rust
if condition { ... } else { ... }
for i in 1..=10 { ... }
while condition { ... }
match value { 1 => ..., 2 => ..., _ => ... }
break;     // EXIT
continue;  // CONTINUE
```

---

## Subroutines and Functions

Subroutines and functions organize code into reusable, named blocks.

> **See also:** [Chapter 7: Breaking It Down](../part1-foundations/07-functions.md) for comprehensive coverage of functions.

### Subroutines (No Return Value)

Use SUB when you want to perform an action without returning a value:

```basic
' Simple subroutine
SUB Greet(name AS STRING)
    PRINT "Hello, " & name & "!"
END SUB

' Calling subroutines
CALL Greet("Alice")    ' Using CALL keyword
Greet "Bob"            ' CALL is optional
Greet("Carol")         ' Parentheses optional for single argument

' Subroutine with multiple parameters
SUB DrawRectangle(x AS INTEGER, y AS INTEGER, width AS INTEGER, height AS INTEGER)
    ' Draw top and bottom
    FOR i = x TO x + width
        PlotPixel(i, y)
        PlotPixel(i, y + height)
    NEXT i
    ' Draw sides
    FOR i = y TO y + height
        PlotPixel(x, i)
        PlotPixel(x + width, i)
    NEXT i
END SUB

DrawRectangle 10, 20, 100, 50
```

### Functions (Return a Value)

Use FUNCTION when you need to compute and return a result:

```basic
' Function with explicit RETURN
FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
    RETURN a + b
END FUNCTION

' Classic BASIC style: assign to function name
FUNCTION Multiply(a AS INTEGER, b AS INTEGER) AS INTEGER
    Multiply = a * b
END FUNCTION

' Using functions
DIM sum AS INTEGER = Add(3, 4)         ' sum = 7
DIM product AS INTEGER = Multiply(3, 4) ' product = 12
PRINT "Result: " & STR(Add(10, 20))     ' Can use directly in expressions

' Function with early return
FUNCTION FindFirst(items() AS STRING, target AS STRING) AS INTEGER
    FOR i = 0 TO UBOUND(items)
        IF items(i) = target THEN
            RETURN i   ' Return immediately when found
        END IF
    NEXT i
    RETURN -1   ' Not found
END FUNCTION
```

### Parameters: BYVAL vs BYREF

```basic
' BYVAL (default): function gets a copy, original unchanged
SUB DoubleByVal(BYVAL x AS INTEGER)
    x = x * 2
    PRINT "Inside: " & STR(x)   ' Shows doubled value
END SUB

DIM num AS INTEGER = 5
DoubleByVal(num)
PRINT "Outside: " & STR(num)    ' Still 5

' BYREF: function gets reference to original, can modify it
SUB DoubleByRef(BYREF x AS INTEGER)
    x = x * 2
END SUB

DIM num2 AS INTEGER = 5
DoubleByRef(num2)
PRINT "After: " & STR(num2)     ' Now 10

' BYREF is essential for multiple "return values"
SUB Divide(dividend AS INTEGER, divisor AS INTEGER, _
           BYREF quotient AS INTEGER, BYREF remainder AS INTEGER)
    quotient = dividend \ divisor
    remainder = dividend MOD divisor
END SUB

DIM q AS INTEGER, r AS INTEGER
Divide 17, 5, q, r
PRINT q; " remainder "; r   ' 3 remainder 2
```

### Optional Parameters

```basic
' Optional parameters have default values
SUB Greet(name AS STRING, OPTIONAL greeting AS STRING = "Hello", _
          OPTIONAL excited AS BOOLEAN = FALSE)
    DIM msg AS STRING = greeting & ", " & name
    IF excited THEN msg = msg & "!"
    PRINT msg
END SUB

Greet "Alice"                     ' "Hello, Alice"
Greet "Bob", "Hi"                 ' "Hi, Bob"
Greet "Carol", "Hey", TRUE        ' "Hey, Carol!"

' Useful for configuration with sensible defaults
FUNCTION CreatePlayer(name AS STRING, _
                      OPTIONAL health AS INTEGER = 100, _
                      OPTIONAL x AS INTEGER = 0, _
                      OPTIONAL y AS INTEGER = 0) AS Player
    DIM p AS Player
    p.name = name
    p.health = health
    p.x = x
    p.y = y
    RETURN p
END FUNCTION

DIM player1 AS Player = CreatePlayer("Hero")
DIM player2 AS Player = CreatePlayer("Wizard", 80, 100, 50)
```

### Recursion

Functions can call themselves for problems with recursive structure:

```basic
' Classic example: factorial
FUNCTION Factorial(n AS INTEGER) AS INTEGER
    IF n <= 1 THEN
        RETURN 1
    ELSE
        RETURN n * Factorial(n - 1)
    END IF
END FUNCTION

PRINT Factorial(5)   ' 120 (5 * 4 * 3 * 2 * 1)

' Fibonacci sequence
FUNCTION Fibonacci(n AS INTEGER) AS INTEGER
    IF n <= 1 THEN RETURN n
    RETURN Fibonacci(n - 1) + Fibonacci(n - 2)
END FUNCTION

' Binary search (recursive)
FUNCTION BinarySearch(arr() AS INTEGER, target AS INTEGER, _
                      low AS INTEGER, high AS INTEGER) AS INTEGER
    IF low > high THEN RETURN -1   ' Not found

    DIM mid AS INTEGER = (low + high) \ 2

    IF arr(mid) = target THEN
        RETURN mid
    ELSEIF arr(mid) > target THEN
        RETURN BinarySearch(arr, target, low, mid - 1)
    ELSE
        RETURN BinarySearch(arr, target, mid + 1, high)
    END IF
END FUNCTION
```

**Zia equivalent:**
```rust
func greet(name: string) {
    Viper.Terminal.Say("Hello, " + name);
}

func add(a: i64, b: i64) -> i64 {
    return a + b;
}

func greet(name: string, greeting: string = "Hello") { ... }
```

---

## Arrays

Arrays store multiple values of the same type in a single variable.

> **See also:** [Chapter 6: Collections](../part1-foundations/06-collections.md) for in-depth array coverage.

### Declaring Arrays

```basic
' Fixed-size array (0 to 10, so 11 elements)
DIM scores(10) AS INTEGER

' Array with explicit bounds
DIM months(1 TO 12) AS STRING

' Multi-dimensional arrays
DIM grid(10, 10) AS INTEGER           ' 11x11 grid (0-10 each)
DIM cube(5, 5, 5) AS INTEGER          ' 3D array

' Initialize with values
DIM primes() AS INTEGER = {2, 3, 5, 7, 11, 13}
DIM names() AS STRING = {"Alice", "Bob", "Carol"}
DIM matrix(,) AS INTEGER = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}
```

### Accessing Array Elements

```basic
DIM values(5) AS INTEGER

' Setting values
values(0) = 10
values(1) = 20
values(2) = 30

' Getting values
DIM first AS INTEGER = values(0)
PRINT values(1)

' Using variables as indices
DIM index AS INTEGER = 2
PRINT values(index)   ' 30

' Multi-dimensional access
DIM board(7, 7) AS STRING
board(0, 0) = "R"    ' Rook in corner
board(3, 3) = "Q"    ' Queen in center
```

### Array Bounds and Size

```basic
DIM items(100) AS INTEGER

' Get bounds
DIM lower AS INTEGER = LBOUND(items)    ' 0
DIM upper AS INTEGER = UBOUND(items)    ' 100

' Calculate size
DIM size AS INTEGER = UBOUND(items) - LBOUND(items) + 1   ' 101 elements

' Works with custom bounds
DIM months(1 TO 12) AS STRING
PRINT LBOUND(months)   ' 1
PRINT UBOUND(months)   ' 12

' Multi-dimensional bounds
DIM matrix(5, 10) AS INTEGER
PRINT UBOUND(matrix, 1)   ' 5 (first dimension)
PRINT UBOUND(matrix, 2)   ' 10 (second dimension)
```

### Dynamic Arrays

```basic
' Declare without size (dynamic)
DIM items() AS STRING

' Allocate space later
REDIM items(10)

' Resize (destroys existing data)
REDIM items(20)

' Resize while preserving data
REDIM PRESERVE items(30)

' Common pattern: growing array
DIM data() AS INTEGER
REDIM data(0)
data(0) = firstValue

FOR i = 1 TO 99
    REDIM PRESERVE data(i)
    data(i) = GetNextValue()
NEXT i
```

### Array Operations

```basic
' Iterate with FOR
DIM numbers() AS INTEGER = {10, 20, 30, 40, 50}

FOR i = LBOUND(numbers) TO UBOUND(numbers)
    PRINT numbers(i)
NEXT i

' Iterate with FOR EACH (cleaner for reading)
FOR EACH num IN numbers
    PRINT num
NEXT num

' Find maximum value
FUNCTION FindMax(arr() AS INTEGER) AS INTEGER
    DIM maxVal AS INTEGER = arr(LBOUND(arr))
    FOR i = LBOUND(arr) + 1 TO UBOUND(arr)
        IF arr(i) > maxVal THEN maxVal = arr(i)
    NEXT i
    RETURN maxVal
END FUNCTION

' Sum all elements
FUNCTION Sum(arr() AS INTEGER) AS INTEGER
    DIM total AS INTEGER = 0
    FOR EACH value IN arr
        total = total + value
    NEXT value
    RETURN total
END FUNCTION

' Copy array
SUB CopyArray(source() AS INTEGER, BYREF dest() AS INTEGER)
    REDIM dest(UBOUND(source))
    FOR i = LBOUND(source) TO UBOUND(source)
        dest(i) = source(i)
    NEXT i
END SUB
```

### Common Array Patterns

```basic
' Initialize all elements to same value
SUB FillArray(arr() AS INTEGER, value AS INTEGER)
    FOR i = LBOUND(arr) TO UBOUND(arr)
        arr(i) = value
    NEXT i
END SUB

' Reverse array in place
SUB ReverseArray(arr() AS INTEGER)
    DIM left AS INTEGER = LBOUND(arr)
    DIM right AS INTEGER = UBOUND(arr)
    DIM temp AS INTEGER

    WHILE left < right
        temp = arr(left)
        arr(left) = arr(right)
        arr(right) = temp
        left = left + 1
        right = right - 1
    WEND
END SUB

' Search for value
FUNCTION IndexOf(arr() AS INTEGER, target AS INTEGER) AS INTEGER
    FOR i = LBOUND(arr) TO UBOUND(arr)
        IF arr(i) = target THEN RETURN i
    NEXT i
    RETURN -1   ' Not found
END FUNCTION
```

**Zia equivalent:**
```rust
var numbers = [1, 2, 3, 4, 5];
var first = numbers[0];
numbers.push(6);
numbers.length;
```

---

## Strings

Strings are sequences of characters for text processing.

> **See also:** [Chapter 8: Text and Strings](../part2-building-blocks/08-strings.md) for comprehensive string handling.

### String Basics

```basic
DIM greeting AS STRING = "Hello, World!"
DIM empty AS STRING = ""
DIM withQuotes AS STRING = "She said ""Hello"""   ' Embedded quotes

' String length
DIM length AS INTEGER = LEN(greeting)   ' 13

' Concatenation
DIM first AS STRING = "Hello"
DIM second AS STRING = "World"
DIM combined AS STRING = first & ", " & second & "!"
```

### Case Conversion

```basic
DIM text AS STRING = "Hello World"

DIM upper AS STRING = UCASE(text)   ' "HELLO WORLD"
DIM lower AS STRING = LCASE(text)   ' "hello world"

' Useful for case-insensitive comparison
IF UCASE(userInput) = "YES" THEN
    PRINT "Confirmed"
END IF
```

### Extracting Substrings

```basic
DIM text AS STRING = "Hello, World!"

' LEFT: Get first N characters
DIM first5 AS STRING = LEFT(text, 5)      ' "Hello"

' RIGHT: Get last N characters
DIM last6 AS STRING = RIGHT(text, 6)      ' "World!"

' MID: Get substring from position (1-based) with length
DIM middle AS STRING = MID(text, 8, 5)    ' "World"

' MID without length: from position to end
DIM rest AS STRING = MID(text, 8)         ' "World!"
```

### Searching Strings

```basic
DIM text AS STRING = "Hello, World! Hello again!"

' Find first occurrence (returns position, 0 if not found)
DIM pos AS INTEGER = INSTR(text, "World")     ' 8

' Find starting from position
DIM pos2 AS INTEGER = INSTR(15, text, "Hello") ' 15 (second Hello)

' Check if contains
IF INSTR(text, "World") > 0 THEN
    PRINT "Contains 'World'"
END IF

' Find from right (last occurrence)
DIM lastPos AS INTEGER = INSTRREV(text, "Hello")  ' 15
```

### Modifying Strings

```basic
' Replace all occurrences
DIM text AS STRING = "Hello World"
DIM newText AS STRING = REPLACE(text, "World", "BASIC")  ' "Hello BASIC"

' Trim whitespace
DIM padded AS STRING = "   Hello   "
DIM trimmed AS STRING = TRIM(padded)      ' "Hello"
DIM leftTrim AS STRING = LTRIM(padded)    ' "Hello   "
DIM rightTrim AS STRING = RTRIM(padded)   ' "   Hello"

' Pad strings
DIM num AS STRING = "42"
DIM padded AS STRING = STRING(5 - LEN(num), "0") & num  ' "00042"
```

### String and Character Conversion

```basic
' Character to ASCII code
DIM code AS INTEGER = ASC("A")     ' 65

' ASCII code to character
DIM char AS STRING = CHR(65)       ' "A"

' Number to string
DIM numStr AS STRING = STR(42)     ' "42" (may have leading space)
DIM numStr2 AS STRING = CSTR(42)   ' "42" (no leading space)

' String to number
DIM num AS INTEGER = VAL("42")     ' 42
DIM num2 AS DOUBLE = VAL("3.14")   ' 3.14
DIM invalid AS INTEGER = VAL("abc") ' 0 (non-numeric returns 0)
```

### String Building and Formatting

```basic
' Build strings efficiently
DIM message AS STRING = ""
message = message & "Name: " & playerName & CHR(10)
message = message & "Score: " & STR(score) & CHR(10)
message = message & "Level: " & STR(level)
PRINT message

' Create repeated characters
DIM line AS STRING = STRING(40, "-")      ' 40 dashes
DIM spaces AS STRING = SPACE(10)          ' 10 spaces

' Format numbers (custom function)
FUNCTION FormatNumber(n AS DOUBLE, decimals AS INTEGER) AS STRING
    DIM factor AS DOUBLE = 10 ^ decimals
    DIM rounded AS DOUBLE = INT(n * factor + 0.5) / factor
    RETURN CSTR(rounded)
END FUNCTION

PRINT FormatNumber(3.14159, 2)   ' "3.14"
```

### Splitting and Joining

```basic
' Split string into array
DIM csv AS STRING = "apple,banana,cherry"
DIM fruits() AS STRING = SPLIT(csv, ",")

FOR EACH fruit IN fruits
    PRINT fruit
NEXT fruit

' Join array into string
DIM joined AS STRING = JOIN(fruits, " | ")  ' "apple | banana | cherry"
```

### Common String Patterns

```basic
' Check if string starts/ends with
FUNCTION StartsWith(text AS STRING, prefix AS STRING) AS BOOLEAN
    RETURN LEFT(text, LEN(prefix)) = prefix
END FUNCTION

FUNCTION EndsWith(text AS STRING, suffix AS STRING) AS BOOLEAN
    RETURN RIGHT(text, LEN(suffix)) = suffix
END FUNCTION

' Extract filename from path
FUNCTION GetFilename(path AS STRING) AS STRING
    DIM pos AS INTEGER = INSTRREV(path, "\")
    IF pos = 0 THEN pos = INSTRREV(path, "/")
    IF pos = 0 THEN RETURN path
    RETURN MID(path, pos + 1)
END FUNCTION

' Word count
FUNCTION WordCount(text AS STRING) AS INTEGER
    DIM words() AS STRING = SPLIT(TRIM(text), " ")
    RETURN UBOUND(words) + 1
END FUNCTION
```

**Zia equivalent:**
```rust
var s = "Hello, World!";
s.toUpperCase();
s.substring(0, 5);
s.contains("World");
s.replace("World", "Viper");
s.split(",");
"Hello, ${name}!";  // String interpolation
```

---

## Input/Output

Reading input and writing output - interacting with users and files.

> **See also:** [Chapter 9: Files and Persistence](../part2-building-blocks/09-files.md) for file operations.

### Console Output

```basic
' Basic output
PRINT "Hello, World!"

' Multiple items (space-separated)
PRINT "Score:"; score; "Lives:"; lives

' Tab-separated columns
PRINT "Name", "Age", "Score"
PRINT "Alice", 25, 1000
PRINT "Bob", 30, 1500

' Suppress newline with semicolon
PRINT "Loading";
FOR i = 1 TO 5
    PRINT ".";
    Delay(500)
NEXT i
PRINT   ' Now print newline

' Print to specific position (if supported)
LOCATE 10, 20   ' Row 10, column 20
PRINT "Centered text"
```

### Console Input

```basic
' String input
DIM name AS STRING
INPUT "Enter your name: ", name
PRINT "Hello, " & name

' Numeric input
DIM age AS INTEGER
INPUT "Enter your age: ", age

' Input without prompt
PRINT "Enter a number:"
INPUT n

' Read entire line (including spaces)
DIM sentence AS STRING
LINE INPUT "Enter a sentence: ", sentence

' Yes/No input pattern
DIM response AS STRING
DO
    INPUT "Continue? (Y/N): ", response
    response = UCASE(response)
LOOP UNTIL response = "Y" OR response = "N"
```

### Input Validation

```basic
' Validate numeric range
FUNCTION GetNumberInRange(prompt AS STRING, min AS INTEGER, max AS INTEGER) AS INTEGER
    DIM value AS INTEGER
    DO
        INPUT prompt, value
        IF value < min OR value > max THEN
            PRINT "Please enter a number between "; min; " and "; max
        END IF
    LOOP UNTIL value >= min AND value <= max
    RETURN value
END FUNCTION

DIM choice AS INTEGER = GetNumberInRange("Enter 1-5: ", 1, 5)

' Validate non-empty string
FUNCTION GetNonEmptyString(prompt AS STRING) AS STRING
    DIM value AS STRING
    DO
        INPUT prompt, value
        value = TRIM(value)
        IF LEN(value) = 0 THEN
            PRINT "Please enter a value"
        END IF
    LOOP UNTIL LEN(value) > 0
    RETURN value
END FUNCTION
```

### File Output

```basic
' Write to file (creates or overwrites)
OPEN "output.txt" FOR OUTPUT AS #1
PRINT #1, "First line"
PRINT #1, "Second line"
PRINT #1, "Score: "; score
CLOSE #1

' Append to file (creates if doesn't exist)
OPEN "log.txt" FOR APPEND AS #1
PRINT #1, NOW; " - User logged in"
CLOSE #1

' Write without newline
OPEN "data.txt" FOR OUTPUT AS #1
WRITE #1, "field1", "field2", 123   ' CSV-style with quotes
CLOSE #1
```

### File Input

```basic
' Read entire file line by line
OPEN "input.txt" FOR INPUT AS #1
WHILE NOT EOF(1)
    LINE INPUT #1, line$
    PRINT line$
WEND
CLOSE #1

' Read formatted data
OPEN "data.txt" FOR INPUT AS #1
INPUT #1, name$, age%, score#
CLOSE #1

' Read with error handling
IF FileExists("data.txt") THEN
    OPEN "data.txt" FOR INPUT AS #1
    ' ... read file ...
    CLOSE #1
ELSE
    PRINT "File not found"
END IF
```

### File Operations

```basic
' Check if file exists
FUNCTION FileExists(filename AS STRING) AS BOOLEAN
    ON ERROR RESUME NEXT
    OPEN filename FOR INPUT AS #99
    IF ERR = 0 THEN
        CLOSE #99
        FileExists = TRUE
    ELSE
        FileExists = FALSE
    END IF
    ON ERROR GOTO 0
END FUNCTION

' Delete file
KILL "oldfile.txt"

' Rename file
NAME "old.txt" AS "new.txt"

' Copy file
SUB CopyFile(source AS STRING, dest AS STRING)
    OPEN source FOR INPUT AS #1
    OPEN dest FOR OUTPUT AS #2
    WHILE NOT EOF(1)
        LINE INPUT #1, line$
        PRINT #2, line$
    WEND
    CLOSE #1
    CLOSE #2
END SUB
```

### Working with Multiple Files

```basic
' Process multiple files
DIM files() AS STRING = {"data1.txt", "data2.txt", "data3.txt"}

FOR EACH filename IN files
    OPEN filename FOR INPUT AS #1
    DIM total AS INTEGER = 0
    WHILE NOT EOF(1)
        INPUT #1, value
        total = total + value
    WEND
    CLOSE #1
    PRINT filename; ": "; total
NEXT filename

' Use different file numbers
OPEN "input.txt" FOR INPUT AS #1
OPEN "output.txt" FOR OUTPUT AS #2

WHILE NOT EOF(1)
    LINE INPUT #1, line$
    PRINT #2, UCASE(line$)
WEND

CLOSE #1
CLOSE #2
```

**Zia equivalent:**
```rust
Viper.Terminal.Say("Hello");
var input = Viper.Terminal.Ask("Name: ");
var file = Viper.File.Open("data.txt");
var content = file.readAll();
file.close();
```

---

## User-Defined Types (Structures)

Group related data together into custom types.

> **See also:** [Chapter 11: Structures](../part2-building-blocks/11-structures.md) for detailed coverage.

### Defining Types

```basic
' Simple type
TYPE Point
    x AS DOUBLE
    y AS DOUBLE
END TYPE

' Type with multiple fields
TYPE Player
    name AS STRING
    health AS INTEGER
    maxHealth AS INTEGER
    x AS SINGLE
    y AS SINGLE
    score AS INTEGER
END TYPE

' Nested types
TYPE Rectangle
    topLeft AS Point
    bottomRight AS Point
END TYPE
```

### Using Types

```basic
' Declare variable of custom type
DIM p AS Point
p.x = 10.0
p.y = 20.0

' Initialize with values
DIM origin AS Point
origin.x = 0.0
origin.y = 0.0

' Access fields
PRINT "Position: "; p.x; ", "; p.y

' Assign entire structure
DIM p2 AS Point
p2 = p   ' Copies all fields
```

### Types with Methods

```basic
TYPE Player
    name AS STRING
    health AS INTEGER
    maxHealth AS INTEGER

    SUB Initialize(playerName AS STRING)
        THIS.name = playerName
        THIS.health = 100
        THIS.maxHealth = 100
    END SUB

    SUB TakeDamage(amount AS INTEGER)
        THIS.health = THIS.health - amount
        IF THIS.health < 0 THEN THIS.health = 0
    END SUB

    SUB Heal(amount AS INTEGER)
        THIS.health = THIS.health + amount
        IF THIS.health > THIS.maxHealth THEN
            THIS.health = THIS.maxHealth
        END IF
    END SUB

    FUNCTION IsAlive() AS BOOLEAN
        RETURN THIS.health > 0
    END FUNCTION
END TYPE

' Using the type
DIM player AS Player
player.Initialize("Hero")
player.TakeDamage(30)
PRINT player.name; " has "; player.health; " health"
IF player.IsAlive() THEN PRINT "Still fighting!"
```

### Arrays of Types

```basic
' Array of custom type
DIM enemies(10) AS Player

FOR i = 0 TO 10
    enemies(i).Initialize("Enemy " & STR(i))
    enemies(i).health = 50
NEXT i

' Process all enemies
FOR EACH enemy IN enemies
    IF enemy.IsAlive() THEN
        UpdateEnemy(enemy)
    END IF
NEXT enemy
```

### Common Type Patterns

```basic
' Vector operations
TYPE Vector2D
    x AS DOUBLE
    y AS DOUBLE

    FUNCTION Add(other AS Vector2D) AS Vector2D
        DIM result AS Vector2D
        result.x = THIS.x + other.x
        result.y = THIS.y + other.y
        RETURN result
    END FUNCTION

    FUNCTION Length() AS DOUBLE
        RETURN SQR(THIS.x * THIS.x + THIS.y * THIS.y)
    END FUNCTION

    SUB Normalize()
        DIM len AS DOUBLE = THIS.Length()
        IF len > 0 THEN
            THIS.x = THIS.x / len
            THIS.y = THIS.y / len
        END IF
    END SUB
END TYPE

' Game entity
TYPE Entity
    id AS INTEGER
    type AS STRING
    x AS SINGLE
    y AS SINGLE
    active AS BOOLEAN

    SUB Update()
        IF NOT THIS.active THEN EXIT SUB
        ' Update logic here
    END SUB
END TYPE
```

**Zia equivalent:**
```rust
value Point {
    x: f64;
    y: f64;

    func distance(other: Point) -> f64 {
        var dx = self.x - other.x;
        var dy = self.y - other.y;
        return Viper.Math.sqrt(dx*dx + dy*dy);
    }
}
```

---

## Classes and Objects

Classes provide encapsulation, constructors, and inheritance for object-oriented programming.

> **See also:** [Chapter 14: Objects and Classes](../part3-objects/14-objects.md) for comprehensive OOP coverage.

### Defining Classes

```basic
CLASS Counter
    PRIVATE count AS INTEGER

    ' Constructor (called when creating new instance)
    SUB New()
        count = 0
    END SUB

    ' Constructor with parameter
    SUB New(initial AS INTEGER)
        count = initial
    END SUB

    ' Methods
    SUB Increment()
        count = count + 1
    END SUB

    SUB Decrement()
        IF count > 0 THEN count = count - 1
    END SUB

    FUNCTION GetCount() AS INTEGER
        RETURN count
    END FUNCTION
END CLASS
```

### Creating and Using Objects

```basic
' Create objects with NEW
DIM counter1 AS Counter = NEW Counter()
DIM counter2 AS Counter = NEW Counter(100)

' Call methods
counter1.Increment()
counter1.Increment()
PRINT counter1.GetCount()   ' 2

counter2.Decrement()
PRINT counter2.GetCount()   ' 99
```

### Access Modifiers

```basic
CLASS BankAccount
    ' Only accessible within this class
    PRIVATE balance AS DOUBLE
    PRIVATE accountNumber AS STRING

    ' Accessible from anywhere
    PUBLIC ownerName AS STRING

    ' Accessible within class and subclasses
    PROTECTED transactionHistory() AS STRING

    SUB New(owner AS STRING, initial AS DOUBLE)
        ownerName = owner
        balance = initial
        accountNumber = GenerateAccountNumber()
    END SUB

    PUBLIC SUB Deposit(amount AS DOUBLE)
        IF amount > 0 THEN
            balance = balance + amount
            LogTransaction("Deposit: " & STR(amount))
        END IF
    END SUB

    PUBLIC FUNCTION GetBalance() AS DOUBLE
        RETURN balance
    END FUNCTION

    PRIVATE FUNCTION GenerateAccountNumber() AS STRING
        ' Internal implementation detail
        RETURN "ACC" & STR(INT(RND() * 1000000))
    END FUNCTION

    PRIVATE SUB LogTransaction(description AS STRING)
        ' Internal logging
    END SUB
END CLASS
```

### Properties (Getters and Setters)

```basic
CLASS Temperature
    PRIVATE celsius AS DOUBLE

    SUB New(c AS DOUBLE)
        celsius = c
    END SUB

    ' Read-only property
    PROPERTY GET Celsius() AS DOUBLE
        RETURN celsius
    END PROPERTY

    ' Read-write property
    PROPERTY GET Fahrenheit() AS DOUBLE
        RETURN celsius * 9 / 5 + 32
    END PROPERTY

    PROPERTY SET Fahrenheit(f AS DOUBLE)
        celsius = (f - 32) * 5 / 9
    END PROPERTY
END CLASS

DIM temp AS Temperature = NEW Temperature(100)
PRINT temp.Celsius      ' 100
PRINT temp.Fahrenheit   ' 212
temp.Fahrenheit = 68    ' Sets via conversion
PRINT temp.Celsius      ' 20
```

### Inheritance

```basic
' Base class
CLASS Animal
    PROTECTED name AS STRING

    SUB New(n AS STRING)
        name = n
    END SUB

    ' Virtual method (can be overridden)
    VIRTUAL SUB Speak()
        PRINT name & " makes a sound"
    END SUB

    SUB Sleep()
        PRINT name & " is sleeping"
    END SUB
END CLASS

' Derived class
CLASS Dog INHERITS Animal
    PRIVATE breed AS STRING

    SUB New(n AS STRING, b AS STRING)
        SUPER.New(n)   ' Call base constructor
        breed = b
    END SUB

    ' Override virtual method
    OVERRIDE SUB Speak()
        PRINT name & " says Woof!"
    END SUB

    SUB Fetch()
        PRINT name & " fetches the ball"
    END SUB
END CLASS

CLASS Cat INHERITS Animal
    SUB New(n AS STRING)
        SUPER.New(n)
    END SUB

    OVERRIDE SUB Speak()
        PRINT name & " says Meow!"
    END SUB
END CLASS

' Using polymorphism
DIM animals(2) AS Animal
animals(0) = NEW Dog("Rex", "German Shepherd")
animals(1) = NEW Cat("Whiskers")
animals(2) = NEW Animal("Unknown")

FOR EACH animal IN animals
    animal.Speak()   ' Calls appropriate override
NEXT animal
```

> **See also:** [Chapter 15: Inheritance](../part3-objects/15-inheritance.md), [Chapter 17: Polymorphism](../part3-objects/17-polymorphism.md)

**Zia equivalent:**
```rust
entity Counter {
    hide count: i64;

    expose func init() {
        self.count = 0;
    }

    func increment() {
        self.count += 1;
    }
}

entity Dog extends Animal {
    override func speak() {
        Viper.Terminal.Say(self.name + " says Woof!");
    }
}
```

---

## Interfaces

Interfaces define contracts that classes must implement.

> **See also:** [Chapter 16: Interfaces](../part3-objects/16-interfaces.md) for detailed interface coverage.

### Defining Interfaces

```basic
INTERFACE Drawable
    SUB Draw()
    FUNCTION GetBounds() AS Rectangle
END INTERFACE

INTERFACE Clickable
    SUB OnClick(x AS INTEGER, y AS INTEGER)
    FUNCTION Contains(x AS INTEGER, y AS INTEGER) AS BOOLEAN
END INTERFACE
```

### Implementing Interfaces

```basic
CLASS Circle IMPLEMENTS Drawable
    PRIVATE centerX AS INTEGER
    PRIVATE centerY AS INTEGER
    PRIVATE radius AS INTEGER

    SUB New(x AS INTEGER, y AS INTEGER, r AS INTEGER)
        centerX = x
        centerY = y
        radius = r
    END SUB

    ' Must implement all interface methods
    SUB Draw()
        DrawCircle(centerX, centerY, radius)
    END SUB

    FUNCTION GetBounds() AS Rectangle
        DIM bounds AS Rectangle
        bounds.topLeft.x = centerX - radius
        bounds.topLeft.y = centerY - radius
        bounds.bottomRight.x = centerX + radius
        bounds.bottomRight.y = centerY + radius
        RETURN bounds
    END FUNCTION
END CLASS

' Implement multiple interfaces
CLASS Button IMPLEMENTS Drawable, Clickable
    PRIVATE x, y, width, height AS INTEGER
    PRIVATE label AS STRING

    SUB Draw()
        DrawRect(x, y, width, height)
        DrawText(x + 5, y + 5, label)
    END SUB

    FUNCTION GetBounds() AS Rectangle
        ' ... implementation ...
    END FUNCTION

    SUB OnClick(clickX AS INTEGER, clickY AS INTEGER)
        PRINT "Button '" & label & "' clicked!"
    END SUB

    FUNCTION Contains(px AS INTEGER, py AS INTEGER) AS BOOLEAN
        RETURN px >= x AND px <= x + width AND _
               py >= y AND py <= y + height
    END FUNCTION
END CLASS
```

### Using Interfaces

```basic
' Variables of interface type
DIM shape AS Drawable = NEW Circle(100, 100, 50)
shape.Draw()

' Collections of interface type
DIM shapes(10) AS Drawable
shapes(0) = NEW Circle(50, 50, 25)
shapes(1) = NEW Button(100, 100, 80, 30, "OK")
' ... more shapes ...

' Draw all shapes (polymorphism)
FOR EACH shape IN shapes
    IF NOT shape IS NOTHING THEN
        shape.Draw()
    END IF
NEXT shape
```

**Zia equivalent:**
```rust
interface Drawable {
    func draw();
    func getBounds() -> Rect;
}

entity Circle implements Drawable {
    func draw() { ... }
    func getBounds() -> Rect { ... }
}
```

---

## Error Handling

Handle runtime errors gracefully to prevent crashes.

> **See also:** [Chapter 10: Errors and Recovery](../part2-building-blocks/10-errors.md) for error handling strategies.

### Traditional BASIC Error Handling

```basic
' Enable error handling
ON ERROR GOTO ErrorHandler

' Risky code
OPEN "file.txt" FOR INPUT AS #1
' ... process file ...
CLOSE #1

' Disable error handling
ON ERROR GOTO 0
EXIT SUB   ' Skip the error handler

ErrorHandler:
    PRINT "Error "; ERR; ": "; ERR.Description
    RESUME NEXT   ' Continue after the error
```

### ON ERROR Options

```basic
' GOTO label: Jump to error handler
ON ERROR GOTO HandleError

' GOTO 0: Disable error handling (errors crash program)
ON ERROR GOTO 0

' RESUME NEXT: Skip error and continue
ON ERROR RESUME NEXT
```

### Error Properties

```basic
ON ERROR GOTO ErrorHandler
' ... code that might fail ...
EXIT SUB

ErrorHandler:
    DIM errorNum AS INTEGER = ERR           ' Error number
    DIM errorMsg AS STRING = ERR.Description ' Error message
    DIM errorLine AS INTEGER = ERL          ' Line number where error occurred

    SELECT CASE errorNum
        CASE 53
            PRINT "File not found"
        CASE 71
            PRINT "Disk not ready"
        CASE ELSE
            PRINT "Error "; errorNum; " at line "; errorLine
    END SELECT

    RESUME NEXT
```

### Modern TRY/CATCH Style

```basic
TRY
    OPEN "data.txt" FOR INPUT AS #1
    LINE INPUT #1, data$
    CLOSE #1
    ProcessData(data$)
CATCH FileNotFoundException
    PRINT "File not found - using defaults"
    UseDefaults()
CATCH ex AS Exception
    PRINT "Error: " & ex.Message
    PRINT "Stack trace: " & ex.StackTrace
FINALLY
    ' Always executes, even if error occurred
    CleanupResources()
END TRY
```

### Throwing Errors

```basic
SUB Withdraw(amount AS DOUBLE)
    IF amount <= 0 THEN
        THROW NEW ArgumentException("Amount must be positive")
    END IF
    IF amount > balance THEN
        THROW NEW InsufficientFundsException("Not enough balance")
    END IF
    balance = balance - amount
END SUB

' Catching specific error
TRY
    account.Withdraw(1000000)
CATCH ex AS InsufficientFundsException
    PRINT "Sorry, insufficient funds"
CATCH ex AS ArgumentException
    PRINT "Invalid amount specified"
END TRY
```

### Error Handling Patterns

```basic
' Pattern: Safe file reading
FUNCTION ReadFileOrDefault(filename AS STRING, default AS STRING) AS STRING
    TRY
        OPEN filename FOR INPUT AS #1
        DIM content AS STRING = ""
        WHILE NOT EOF(1)
            DIM line AS STRING
            LINE INPUT #1, line
            content = content & line & CHR(10)
        WEND
        CLOSE #1
        RETURN content
    CATCH
        RETURN default
    END TRY
END FUNCTION

' Pattern: Retry logic
FUNCTION TryConnect(maxAttempts AS INTEGER) AS BOOLEAN
    DIM attempt AS INTEGER
    FOR attempt = 1 TO maxAttempts
        TRY
            Connect()
            RETURN TRUE
        CATCH
            PRINT "Attempt "; attempt; " failed"
            IF attempt < maxAttempts THEN Delay(1000)
        END TRY
    NEXT attempt
    RETURN FALSE
END FUNCTION

' Pattern: Resource cleanup
SUB ProcessWithCleanup()
    DIM resource AS Resource = AcquireResource()
    TRY
        DoSomethingRisky(resource)
    FINALLY
        resource.Release()   ' Always called
    END TRY
END SUB
```

**Zia equivalent:**
```rust
try {
    var file = Viper.File.Open("data.txt");
    // ... use file ...
} catch FileNotFoundError {
    Viper.Terminal.Say("File not found");
} finally {
    cleanup();
}

if condition { throw CustomError("message"); }
```

---

## Built-in Functions

Viper BASIC includes many built-in functions for common operations.

> **See also:** [Chapter 13: The Standard Library](../part2-building-blocks/13-stdlib.md), [Appendix D: Runtime Library](d-runtime-reference.md)

### Math Functions

```basic
' Basic math
PRINT ABS(-5)         ' Absolute value: 5
PRINT SGN(-5)         ' Sign: -1 (returns -1, 0, or 1)
PRINT INT(3.7)        ' Integer part (floor): 3
PRINT INT(-3.7)       ' Floor for negatives: -4
PRINT FIX(3.7)        ' Truncate toward zero: 3
PRINT FIX(-3.7)       ' Truncate toward zero: -3

' Rounding
PRINT ROUND(3.5)      ' Round to nearest: 4
PRINT ROUND(3.14159, 2) ' Round to decimals: 3.14

' Powers and roots
PRINT SQR(16)         ' Square root: 4
PRINT 2 ^ 10          ' Power: 1024
PRINT LOG(2.718)      ' Natural log: ~1
PRINT EXP(1)          ' e^x: 2.718...

' Trigonometry (angles in radians)
CONST PI = 3.14159265359
PRINT SIN(PI / 2)     ' Sine: 1
PRINT COS(0)          ' Cosine: 1
PRINT TAN(PI / 4)     ' Tangent: ~1
PRINT ATN(1) * 4      ' Arctangent: PI

' Converting degrees to radians
FUNCTION ToRadians(degrees AS DOUBLE) AS DOUBLE
    RETURN degrees * PI / 180
END FUNCTION

' Random numbers
RANDOMIZE TIMER       ' Seed random generator
PRINT RND()           ' Random 0 to 1
PRINT INT(RND() * 100) + 1  ' Random 1 to 100

' Min/Max
PRINT MIN(5, 10)      ' Minimum: 5
PRINT MAX(5, 10)      ' Maximum: 10

' Clamping value to range
FUNCTION Clamp(value AS DOUBLE, minVal AS DOUBLE, maxVal AS DOUBLE) AS DOUBLE
    RETURN MAX(minVal, MIN(value, maxVal))
END FUNCTION
```

### String Functions

```basic
' Length and case
DIM s AS STRING = "Hello World"
PRINT LEN(s)          ' Length: 11
PRINT UCASE(s)        ' Uppercase: "HELLO WORLD"
PRINT LCASE(s)        ' Lowercase: "hello world"

' Alternative syntax with $ suffix
PRINT UCASE$(s)       ' Same as UCASE
PRINT LCASE$(s)       ' Same as LCASE

' Trimming
DIM padded AS STRING = "  hello  "
PRINT "[" & TRIM(padded) & "]"   ' "[hello]"
PRINT "[" & LTRIM(padded) & "]"  ' "[hello  ]"
PRINT "[" & RTRIM(padded) & "]"  ' "[  hello]"

' Extracting
DIM text AS STRING = "Hello, World!"
PRINT LEFT(text, 5)   ' Left 5 chars: "Hello"
PRINT RIGHT(text, 6)  ' Right 6 chars: "World!"
PRINT MID(text, 8, 5) ' Substring: "World"
PRINT MID(text, 8)    ' From position to end: "World!"

' Searching
PRINT INSTR(text, "World")    ' Find position: 8
PRINT INSTR(1, text, "o")     ' First 'o': 5
PRINT INSTR(6, text, "o")     ' Next 'o': 9
PRINT INSTRREV(text, "o")     ' Last 'o': 9

' Replacing
PRINT REPLACE(text, "World", "BASIC")  ' "Hello, BASIC!"

' Splitting and joining
DIM parts() AS STRING = SPLIT("a,b,c", ",")
PRINT JOIN(parts, "-")   ' "a-b-c"

' Character conversion
PRINT ASC("A")        ' ASCII code: 65
PRINT CHR(65)         ' Character: "A"
PRINT CHR(10)         ' Newline character

' String creation
PRINT STRING(10, "-") ' Repeat character: "----------"
PRINT STRING(5, 65)   ' Repeat by code: "AAAAA"
PRINT SPACE(5)        ' Spaces: "     "
```

### Conversion Functions

```basic
' To integer
PRINT CINT(3.7)       ' Convert to integer: 4 (rounds)
PRINT INT(3.7)        ' Floor: 3

' To long
PRINT CLNG(3.7)       ' Convert to long: 4

' To float
PRINT CSNG(42)        ' Convert to single: 42.0
PRINT CDBL(42)        ' Convert to double: 42.0

' To string
PRINT CSTR(42)        ' Convert to string: "42"
PRINT STR(42)         ' With space for sign: " 42"

' To boolean
PRINT CBOOL(0)        ' FALSE
PRINT CBOOL(1)        ' TRUE
PRINT CBOOL(-1)       ' TRUE

' String to number
PRINT VAL("42")       ' Parse string: 42
PRINT VAL("3.14")     ' Parse float: 3.14
PRINT VAL("42abc")    ' Stops at non-digit: 42
PRINT VAL("abc")      ' No digits: 0

' Hexadecimal
PRINT HEX(255)        ' To hex string: "FF"
PRINT "&H" & HEX(255) ' With prefix: "&HFF"
PRINT VAL("&HFF")     ' Parse hex: 255

' Binary
PRINT BIN(255)        ' To binary string: "11111111"
```

### Date and Time Functions

```basic
' Current date/time
DIM today AS DATE = NOW
PRINT DATE$           ' Current date as string
PRINT TIME$           ' Current time as string
PRINT TIMER           ' Seconds since midnight

' Extract components
PRINT YEAR(NOW)       ' Current year
PRINT MONTH(NOW)      ' Current month (1-12)
PRINT DAY(NOW)        ' Current day (1-31)
PRINT WEEKDAY(NOW)    ' Day of week (1=Sunday)
PRINT HOUR(NOW)       ' Hour (0-23)
PRINT MINUTE(NOW)     ' Minute (0-59)
PRINT SECOND(NOW)     ' Second (0-59)

' Create date
DIM birthday AS DATE = DATESERIAL(1990, 6, 15)  ' Year, Month, Day
DIM meeting AS DATE = TIMESERIAL(14, 30, 0)      ' Hour, Minute, Second

' Date arithmetic
DIM tomorrow AS DATE = NOW + 1
DIM nextWeek AS DATE = NOW + 7
DIM daysUntil AS INTEGER = birthday - NOW

' Formatting
PRINT FORMAT(NOW, "yyyy-mm-dd")         ' "2024-01-15"
PRINT FORMAT(NOW, "hh:mm:ss")           ' "14:30:00"
PRINT FORMAT(NOW, "dddd, mmmm d, yyyy") ' "Monday, January 15, 2024"
```

### Utility Functions

```basic
' Type checking
DIM x = 42
PRINT TYPENAME(x)     ' "Integer"
PRINT VARTYPE(x)      ' Type code

DIM obj AS Object
PRINT obj IS NOTHING  ' TRUE if null

' Array functions
DIM arr(10) AS INTEGER
PRINT LBOUND(arr)     ' Lower bound: 0
PRINT UBOUND(arr)     ' Upper bound: 10
ERASE arr             ' Clear array

' Program control
END                   ' End program
STOP                  ' Break (debugging)
SLEEP 1000            ' Pause 1000 milliseconds
```

---

## Modules

Organize code into separate, reusable modules.

> **See also:** [Chapter 12: Modules](../part2-building-blocks/12-modules.md) for module organization.

### Defining a Module

```basic
MODULE MathUtils

' Public - accessible from other modules
PUBLIC FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
    RETURN a + b
END FUNCTION

PUBLIC FUNCTION Multiply(a AS INTEGER, b AS INTEGER) AS INTEGER
    RETURN a * b
END FUNCTION

' Private - only accessible within this module
PRIVATE FUNCTION Validate(n AS INTEGER) AS BOOLEAN
    RETURN n >= 0
END FUNCTION

' Module-level variables
PRIVATE calculationCount AS INTEGER = 0

PUBLIC SUB ResetCounter()
    calculationCount = 0
END SUB

END MODULE
```

### Using Modules

```basic
' Import entire module
IMPORT MathUtils

' Use with module prefix
DIM result AS INTEGER = MathUtils.Add(3, 4)

' Import specific items
IMPORT MathUtils.Add
IMPORT MathUtils.Multiply

' Use directly (no prefix needed)
DIM sum AS INTEGER = Add(3, 4)
DIM product AS INTEGER = Multiply(3, 4)

' Import with alias
IMPORT MathUtils AS Math

DIM result AS INTEGER = Math.Add(3, 4)
```

### Module Organization Patterns

```basic
' Main module (entry point)
MODULE MyApplication

IMPORT GameEngine
IMPORT UIComponents
IMPORT DataManager

SUB Main()
    DataManager.Initialize()
    UIComponents.ShowSplashScreen()
    GameEngine.Start()
END SUB

END MODULE

' Separate module for game logic
MODULE GameEngine

PRIVATE running AS BOOLEAN = FALSE

PUBLIC SUB Start()
    running = TRUE
    WHILE running
        Update()
        Render()
    WEND
END SUB

PUBLIC SUB Stop()
    running = FALSE
END SUB

PRIVATE SUB Update()
    ' Game logic
END SUB

PRIVATE SUB Render()
    ' Drawing
END SUB

END MODULE
```

**Zia equivalent:**
```rust
module MathUtils;

export func add(a: i64, b: i64) -> i64 {
    return a + b;
}

func privateHelper() { ... }  // Not exported

// In another file:
import MathUtils;
var result = MathUtils.add(3, 4);
```

---

## Special Syntax

Miscellaneous syntax features and conveniences.

### Line Continuation

Use underscore to continue long lines:

```basic
' Long string
DIM message AS STRING = "This is a very long message that " & _
    "spans multiple lines in the source code but " & _
    "will be a single string at runtime"

' Long condition
IF playerHealth > 0 AND playerAmmo > 0 AND _
   NOT playerStunned AND gameState = PLAYING THEN
    ProcessPlayerInput()
END IF

' Long function call
result = CalculateComplexValue( _
    param1, _
    param2, _
    param3, _
    param4 _
)
```

### Multiple Statements on One Line

Use colon to separate statements:

```basic
' Multiple assignments
x = 1 : y = 2 : z = 3

' Compact initialization
DIM a AS INTEGER = 0 : DIM b AS INTEGER = 0 : DIM c AS INTEGER = 0

' Short conditionals
IF error THEN PRINT "Error!" : EXIT SUB
```

### With Blocks

Simplify repeated access to object members:

```basic
' Without WITH (repetitive)
player.x = 100
player.y = 200
player.health = 100
player.name = "Hero"
player.Initialize()

' With WITH block (cleaner)
WITH player
    .x = 100
    .y = 200
    .health = 100
    .name = "Hero"
    .Initialize()
END WITH

' Nested WITH
WITH game
    .title = "Adventure"
    WITH .player
        .x = 100
        .y = 200
    END WITH
END WITH
```

### OPTION Statements

Configure compiler behavior:

```basic
' Require all variables to be declared
OPTION EXPLICIT

' Set default array lower bound
OPTION BASE 1   ' Arrays start at 1 instead of 0

' String comparison mode
OPTION COMPARE TEXT    ' Case-insensitive
OPTION COMPARE BINARY  ' Case-sensitive (default)
```

### Labels and GOTO

Use sparingly - prefer structured control flow:

```basic
' Labels for error handling
ON ERROR GOTO ErrorHandler
' ... code ...
EXIT SUB

ErrorHandler:
    PRINT "Error occurred"
    RESUME NEXT

' GOSUB/RETURN (old-style subroutines)
GOSUB PrintHeader
' ... code ...
END

PrintHeader:
    PRINT "=== Header ==="
    RETURN
```

### Comments in Different Styles

```basic
' Single quote comment (preferred)

REM Traditional BASIC comment

' Multi-line comments using continuation
' This is a longer comment that spans
' multiple lines. Each line needs its
' own comment marker.
```

---

## Common Patterns

Idiomatic patterns for common programming tasks.

### Input Validation Loop

```basic
' Generic validated input
FUNCTION GetValidInput(prompt AS STRING, _
                       validator AS FUNCTION(STRING) AS BOOLEAN, _
                       errorMsg AS STRING) AS STRING
    DIM input AS STRING
    DO
        INPUT prompt, input
        IF NOT validator(input) THEN
            PRINT errorMsg
        END IF
    LOOP UNTIL validator(input)
    RETURN input
END FUNCTION

' Simple numeric range validation
DIM age AS INTEGER
DO
    INPUT "Enter your age (0-150): ", age
LOOP UNTIL age >= 0 AND age <= 150
```

### Menu-Driven Program

```basic
SUB ShowMenu()
    PRINT ""
    PRINT "=== Main Menu ==="
    PRINT "1. New Game"
    PRINT "2. Load Game"
    PRINT "3. Options"
    PRINT "4. Quit"
    PRINT ""
END SUB

SUB Main()
    DIM choice AS INTEGER
    DO
        ShowMenu()
        INPUT "Enter choice: ", choice

        SELECT CASE choice
            CASE 1
                StartNewGame()
            CASE 2
                LoadGame()
            CASE 3
                ShowOptions()
            CASE 4
                PRINT "Goodbye!"
            CASE ELSE
                PRINT "Invalid choice"
        END SELECT
    LOOP UNTIL choice = 4
END SUB
```

### Game Loop

```basic
SUB GameLoop()
    DIM running AS BOOLEAN = TRUE
    DIM lastTime AS DOUBLE = TIMER
    DIM deltaTime AS DOUBLE

    WHILE running
        ' Calculate time since last frame
        DIM currentTime AS DOUBLE = TIMER
        deltaTime = currentTime - lastTime
        lastTime = currentTime

        ' Process input
        DIM key AS STRING = INKEY$
        IF key = CHR(27) THEN running = FALSE  ' ESC to quit

        ' Update game state
        UpdateGame(deltaTime)

        ' Render
        ClearScreen()
        DrawGame()

        ' Cap frame rate
        SLEEP 16   ' ~60 FPS
    WEND
END SUB
```

### Configuration File Handler

```basic
TYPE Config
    playerName AS STRING
    difficulty AS INTEGER
    soundEnabled AS BOOLEAN
    musicVolume AS INTEGER
END TYPE

FUNCTION LoadConfig(filename AS STRING) AS Config
    DIM cfg AS Config
    ' Set defaults
    cfg.playerName = "Player"
    cfg.difficulty = 1
    cfg.soundEnabled = TRUE
    cfg.musicVolume = 80

    TRY
        OPEN filename FOR INPUT AS #1
        WHILE NOT EOF(1)
            DIM line AS STRING
            LINE INPUT #1, line
            DIM parts() AS STRING = SPLIT(line, "=")
            IF UBOUND(parts) >= 1 THEN
                DIM key AS STRING = TRIM(parts(0))
                DIM value AS STRING = TRIM(parts(1))
                SELECT CASE UCASE(key)
                    CASE "PLAYERNAME"
                        cfg.playerName = value
                    CASE "DIFFICULTY"
                        cfg.difficulty = VAL(value)
                    CASE "SOUND"
                        cfg.soundEnabled = (UCASE(value) = "TRUE")
                    CASE "MUSICVOLUME"
                        cfg.musicVolume = VAL(value)
                END SELECT
            END IF
        WEND
        CLOSE #1
    CATCH
        ' Use defaults if file not found
    END TRY

    RETURN cfg
END FUNCTION

SUB SaveConfig(cfg AS Config, filename AS STRING)
    OPEN filename FOR OUTPUT AS #1
    PRINT #1, "PlayerName=" & cfg.playerName
    PRINT #1, "Difficulty=" & STR(cfg.difficulty)
    PRINT #1, "Sound=" & IIF(cfg.soundEnabled, "TRUE", "FALSE")
    PRINT #1, "MusicVolume=" & STR(cfg.musicVolume)
    CLOSE #1
END SUB
```

### Simple State Machine

```basic
CONST STATE_MENU = 0
CONST STATE_PLAYING = 1
CONST STATE_PAUSED = 2
CONST STATE_GAMEOVER = 3

DIM currentState AS INTEGER = STATE_MENU

SUB UpdateGame()
    SELECT CASE currentState
        CASE STATE_MENU
            UpdateMenu()
        CASE STATE_PLAYING
            UpdatePlaying()
        CASE STATE_PAUSED
            UpdatePaused()
        CASE STATE_GAMEOVER
            UpdateGameOver()
    END SELECT
END SUB

SUB ChangeState(newState AS INTEGER)
    ' Exit current state
    SELECT CASE currentState
        CASE STATE_PLAYING
            PauseMusic()
    END SELECT

    ' Enter new state
    SELECT CASE newState
        CASE STATE_PLAYING
            ResumeMusic()
        CASE STATE_GAMEOVER
            PlaySound("gameover.wav")
    END SELECT

    currentState = newState
END SUB
```

### Object Pool Pattern

```basic
' Reuse objects instead of creating/destroying
CLASS BulletPool
    PRIVATE bullets(100) AS Bullet
    PRIVATE activeCount AS INTEGER = 0

    SUB New()
        FOR i = 0 TO 99
            bullets(i) = NEW Bullet()
            bullets(i).active = FALSE
        NEXT i
    END SUB

    FUNCTION GetBullet() AS Bullet
        ' Find inactive bullet
        FOR i = 0 TO 99
            IF NOT bullets(i).active THEN
                bullets(i).active = TRUE
                activeCount = activeCount + 1
                RETURN bullets(i)
            END IF
        NEXT i
        RETURN NOTHING   ' Pool exhausted
    END FUNCTION

    SUB ReturnBullet(bullet AS Bullet)
        bullet.active = FALSE
        activeCount = activeCount - 1
    END SUB

    SUB UpdateAll()
        FOR i = 0 TO 99
            IF bullets(i).active THEN
                bullets(i).Update()
                IF bullets(i).IsOffScreen() THEN
                    ReturnBullet(bullets(i))
                END IF
            END IF
        NEXT i
    END SUB
END CLASS
```

---

## Keywords Reference

Complete alphabetical list of Viper BASIC reserved keywords:

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

### Keywords by Category

| Category | Keywords |
|----------|----------|
| **Data Types** | `BOOLEAN`, `BYTE`, `DATE`, `DOUBLE`, `INTEGER`, `LONG`, `SINGLE`, `STRING`, `VARIANT` |
| **Declarations** | `AS`, `CONST`, `DIM`, `FUNCTION`, `GLOBAL`, `LOCAL`, `PRIVATE`, `PROTECTED`, `PUBLIC`, `SHARED`, `STATIC`, `SUB`, `TYPE` |
| **Control Flow** | `CASE`, `DO`, `EACH`, `ELSE`, `ELSEIF`, `END`, `EXIT`, `FOR`, `GOSUB`, `GOTO`, `IF`, `LOOP`, `NEXT`, `RETURN`, `SELECT`, `STEP`, `THEN`, `TO`, `UNTIL`, `WEND`, `WHILE` |
| **Operators** | `AND`, `IS`, `MOD`, `NOT`, `OR`, `XOR` |
| **Error Handling** | `CATCH`, `ERROR`, `FINALLY`, `ON`, `RESUME`, `THROW`, `TRY` |
| **OOP** | `CLASS`, `IMPLEMENTS`, `INHERITS`, `INTERFACE`, `NEW`, `NOTHING`, `OVERRIDE`, `PROPERTY`, `SUPER`, `THIS`, `VIRTUAL` |
| **Parameters** | `BYREF`, `BYVAL`, `OPTIONAL` |
| **Files** | `CLOSE`, `EOF`, `GET`, `INPUT`, `LINE`, `OPEN`, `OUTPUT`, `PRINT`, `PUT`, `WRITE` |
| **Arrays** | `ERASE`, `PRESERVE`, `REDIM` |
| **Modules** | `IMPORT`, `MODULE` |
| **Other** | `CALL`, `CONTINUE`, `DECLARE`, `FALSE`, `IN`, `LET`, `OPTION`, `RANDOMIZE`, `REM`, `SET`, `STOP`, `TRUE`, `WITH` |

---

## Quick Reference Card

### Variable Declaration
```basic
DIM name AS STRING = "value"
CONST PI = 3.14159
```

### Conditionals
```basic
IF condition THEN ... ELSEIF ... ELSE ... END IF
SELECT CASE value ... CASE ... CASE ELSE ... END SELECT
```

### Loops
```basic
FOR i = 1 TO 10 STEP 1 ... NEXT i
FOR EACH item IN collection ... NEXT item
WHILE condition ... WEND
DO WHILE/UNTIL condition ... LOOP
```

### Functions
```basic
FUNCTION Name(param AS TYPE) AS RETURNTYPE ... END FUNCTION
SUB Name(param AS TYPE) ... END SUB
```

### Classes
```basic
CLASS Name ... PRIVATE/PUBLIC ... SUB New() ... END CLASS
DIM obj AS Name = NEW Name()
```

### Error Handling
```basic
TRY ... CATCH ... FINALLY ... END TRY
ON ERROR GOTO label / RESUME NEXT
```

---

*[Back to Table of Contents](../README.md) | [Prev: Appendix A](a-zia-reference.md) | [Next: Appendix C: Pascal Reference](c-pascal-reference.md)*
