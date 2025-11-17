REM Test recursive functions
DIM result AS INTEGER

PRINT "=== RECURSION TEST ==="
PRINT ""

FUNCTION Factorial(n AS INTEGER) AS INTEGER
    IF n <= 1 THEN
        Factorial = 1
    ELSE
        Factorial = n * Factorial(n - 1)
    END IF
END FUNCTION

FUNCTION Fibonacci(n AS INTEGER) AS INTEGER
    IF n <= 1 THEN
        Fibonacci = n
    ELSE
        Fibonacci = Fibonacci(n - 1) + Fibonacci(n - 2)
    END IF
END FUNCTION

PRINT "Testing Factorial:"
DIM i AS INTEGER
FOR i = 1 TO 7
    result = Factorial(i)
    PRINT i; "! = "; result
NEXT
PRINT ""

PRINT "Testing Fibonacci:"
FOR i = 0 TO 10
    result = Fibonacci(i)
    PRINT "Fib("; i; ") = "; result
NEXT
PRINT ""

PRINT "Recursion test complete!"
