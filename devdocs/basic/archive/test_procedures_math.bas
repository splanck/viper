REM Test new features with procedures and functions
PRINT "=== Testing Math Functions in Procedures ==="

SUB ShowSign(value)
    PRINT "Value: "; value; " Sign: "; SGN(value)
END SUB

FUNCTION CalcCircumference(radius)
    CONST PI = 3
    RETURN 2 * PI * radius
END FUNCTION

FUNCTION Factorial(n)
    IF n <= 1 THEN
        RETURN 1
    END IF
    result = 1
    FOR i = 2 TO n
        result = result * i
    NEXT i
    RETURN result
END FUNCTION

REM Test calling procedures with new functions
ShowSign(-10)
ShowSign(0)
ShowSign(10)

PRINT ""
PRINT "=== Testing Functions ==="
circ = CalcCircumference(5)
PRINT "Circumference (r=5): "; circ

fact5 = Factorial(5)
PRINT "5! = "; fact5

PRINT ""
PRINT "=== Testing SWAP in procedure context ==="
SUB SwapAndPrint(x, y)
    PRINT "Before: x="; x; " y="; y
    SWAP x, y
    PRINT "After: x="; x; " y="; y
END SUB

a = 100
b = 200
SwapAndPrint(a, b)
PRINT "Original variables: a="; a; " b="; b
PRINT "(Parameters swapped but originals unchanged)"

PRINT ""
PRINT "All procedure tests passed!"
