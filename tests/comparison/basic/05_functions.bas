' Test: Functions and Procedures
' Tests: FUNCTION, SUB, parameters, return values, recursion

' Simple function
FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
    Add = a + b
END FUNCTION

' Function with RETURN
FUNCTION Multiply(a AS INTEGER, b AS INTEGER) AS INTEGER
    RETURN a * b
END FUNCTION

' SUB (procedure)
SUB PrintMessage(msg AS STRING)
    PRINT "Message: "; msg
END SUB

' ByRef parameter
SUB Increment(BYREF x AS INTEGER)
    x = x + 1
END SUB

' Recursive function
FUNCTION Factorial(n AS INTEGER) AS INTEGER
    IF n <= 1 THEN
        Factorial = 1
    ELSE
        Factorial = n * Factorial(n - 1)
    END IF
END FUNCTION

' Function returning string
FUNCTION Greet$(name AS STRING) AS STRING
    Greet$ = "Hello, " + name + "!"
END FUNCTION

' Main program
PRINT "=== Functions Test ==="

PRINT ""
PRINT "Simple function: Add(3, 5) = "; Add(3, 5)
PRINT "With RETURN: Multiply(4, 6) = "; Multiply(4, 6)

PRINT ""
PrintMessage("Hello from SUB")

PRINT ""
PRINT "ByRef test:"
DIM value AS INTEGER
value = 10
PRINT "  Before: "; value
Increment(value)
PRINT "  After: "; value

PRINT ""
PRINT "Recursion: Factorial(5) = "; Factorial(5)

PRINT ""
PRINT "String function: "; Greet$("World")

PRINT "=== Functions test complete ==="
