' Comprehensive Test 3: Control Flow and String Operations
' Tests: IF/ELSE, SELECT CASE, WHILE, DO loops, FOR with STEP, string functions
' Expected to produce deterministic output for both VM and native execution

DIM i AS INTEGER
DIM j AS INTEGER
DIM s AS STRING
DIM result AS STRING

PRINT "=== IF/ELSE Tests ==="
FOR i = 1 TO 10
    IF i MOD 3 = 0 AND i MOD 5 = 0 THEN
        PRINT "FizzBuzz"
    ELSE IF i MOD 3 = 0 THEN
        PRINT "Fizz"
    ELSE IF i MOD 5 = 0 THEN
        PRINT "Buzz"
    ELSE
        PRINT i
    END IF
NEXT i

PRINT "=== Nested IF Tests ==="
FOR i = 1 TO 5
    FOR j = 1 TO 5
        IF i = j THEN
            PRINT "D"
        ELSE IF i < j THEN
            PRINT "U"
        ELSE
            PRINT "L"
        END IF
    NEXT j
NEXT i

PRINT "=== SELECT CASE Tests ==="
FOR i = 0 TO 10
    SELECT CASE i
        CASE 0
            PRINT "zero"
        CASE 1, 2, 3
            PRINT "small"
        CASE 4 TO 6
            PRINT "medium"
        CASE 7, 8
            PRINT "large"
        CASE ELSE
            PRINT "huge"
    END SELECT
NEXT i

PRINT "=== WHILE Loop Tests ==="
i = 1
DIM sum AS INTEGER
sum = 0
WHILE i <= 10
    sum = sum + i
    i = i + 1
WEND
PRINT sum

' Countdown
i = 5
WHILE i > 0
    PRINT i
    i = i - 1
WEND

PRINT "=== DO WHILE Tests ==="
i = 1
sum = 0
DO WHILE i <= 5
    sum = sum + i * i
    i = i + 1
LOOP
PRINT sum

PRINT "=== DO UNTIL Tests ==="
i = 10
DO UNTIL i < 1
    PRINT i
    i = i - 2
LOOP

PRINT "=== FOR STEP Tests ==="
' Positive step
FOR i = 0 TO 20 STEP 4
    PRINT i
NEXT i

' Negative step
FOR i = 10 TO 0 STEP -2
    PRINT i
NEXT i

PRINT "=== EXIT FOR Tests ==="
FOR i = 1 TO 100
    IF i > 5 THEN
        EXIT FOR
    END IF
    PRINT i
NEXT i

PRINT "=== EXIT WHILE Tests ==="
i = 1
WHILE i < 100
    IF i > 3 THEN
        EXIT WHILE
    END IF
    PRINT i
    i = i + 1
WEND

PRINT "=== String Length Tests ==="
s = "Hello"
PRINT LEN(s)
s = "X"
PRINT LEN(s)
s = "Hello World!"
PRINT LEN(s)

PRINT "=== String Concatenation ==="
s = "Hello" + " " + "World"
PRINT s
s = "A" + "B" + "C" + "D" + "E"
PRINT s

PRINT "=== LEFT/RIGHT/MID Tests ==="
s = "ABCDEFGHIJ"
PRINT LEFT$(s, 3)
PRINT RIGHT$(s, 3)
PRINT MID$(s, 4, 3)
PRINT MID$(s, 1, 5)

PRINT "=== UCASE/LCASE Tests ==="
s = "Hello World"
PRINT UCASE$(s)
PRINT LCASE$(s)

PRINT "=== TRIM Tests ==="
s = "   spaces   "
PRINT LTRIM$(s)
PRINT RTRIM$(s)
PRINT TRIM$(s)

PRINT "=== INSTR Tests ==="
s = "Hello World Hello"
PRINT INSTR(s, "World")
PRINT INSTR(s, "Hello")
PRINT INSTR(s, "xyz")

PRINT "=== CHR$/ASC Tests ==="
FOR i = 65 TO 70
    PRINT CHR$(i)
NEXT i
PRINT ASC("A")
PRINT ASC("Z")
PRINT ASC("a")

PRINT "=== STR$/VAL Tests ==="
PRINT STR$(123)
PRINT STR$(-456)
PRINT VAL("789")
PRINT VAL("-123")
PRINT VAL("3.14")

PRINT "=== String Comparison ==="
IF "apple" < "banana" THEN
    PRINT "apple < banana"
END IF
IF "ABC" = "ABC" THEN
    PRINT "ABC = ABC"
END IF
IF "xyz" > "abc" THEN
    PRINT "xyz > abc"
END IF

PRINT "=== Complex Control Flow ==="
' Nested loops with multiple exit conditions
DIM found AS INTEGER
found = 0
FOR i = 1 TO 10
    FOR j = 1 TO 10
        IF i * j = 42 THEN
            found = 1
            EXIT FOR
        END IF
    NEXT j
    IF found = 1 THEN
        EXIT FOR
    END IF
NEXT i
PRINT "Found at:"
PRINT i
PRINT j

PRINT "=== GOSUB/RETURN Tests ==="
DIM counter AS INTEGER
counter = 0
GOSUB IncrementCounter
GOSUB IncrementCounter
GOSUB IncrementCounter
PRINT counter
GOTO SkipSub

IncrementCounter:
    counter = counter + 10
    RETURN

SkipSub:

PRINT "=== Arithmetic Expressions ==="
DIM a AS INTEGER
DIM b AS INTEGER
DIM c AS INTEGER
a = 10
b = 3
c = 2
PRINT a + b * c
PRINT (a + b) * c
PRINT a MOD b
PRINT a / b
PRINT a \ b

PRINT "=== Boolean Expressions ==="
a = 5
b = 10
c = 5
IF a = c AND b > a THEN
    PRINT "T1"
END IF
IF a > b OR c = a THEN
    PRINT "T2"
END IF
IF NOT (a > b) THEN
    PRINT "T3"
END IF

PRINT "=== Test Complete ==="
