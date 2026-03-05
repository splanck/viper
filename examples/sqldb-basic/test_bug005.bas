' Test case for Bug #005: EXIT FUNCTION in class methods causes missing return
' This test has NO backslashes to rule out Bug #007 corruption

CLASS Calculator
    PUBLIC FUNCTION Divide(a AS INTEGER, b AS INTEGER) AS INTEGER
        IF b = 0 THEN
            PRINT "Error: Division by zero"
            Divide = 0
            EXIT FUNCTION
        END IF
        Divide = a / b
    END FUNCTION

    PUBLIC FUNCTION Factorial(n AS INTEGER) AS INTEGER
        IF n <= 1 THEN
            Factorial = 1
            EXIT FUNCTION
        END IF
        Factorial = n * Factorial(n - 1)
    END FUNCTION
END CLASS

DIM calc AS Calculator
LET calc = NEW Calculator()

PRINT "Testing Bug #005:"
PRINT "Divide(10, 2) = "; calc.Divide(10, 2); " (expect 5)"
PRINT "Divide(10, 0) = "; calc.Divide(10, 0); " (expect 0)"
PRINT "Factorial(5) = "; calc.Factorial(5); " (expect 120)"
PRINT "Bug #005 test complete"
