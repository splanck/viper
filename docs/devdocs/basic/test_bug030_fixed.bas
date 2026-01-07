REM Test BUG-030 FIX: Global variable sharing across SUB/FUNCTION
REM Tests that module-level variables are properly shared

REM Test 1: Integer global
DIM X AS INTEGER
X = 1
PRINT "Main initial: X = "; X

SUB IncrementX()
    X = X + 1
    PRINT "In SUB: X = "; X
END SUB

IncrementX()
PRINT "Main after SUB: X = "; X
PRINT ""

REM Test 2: Float global
DIM PI AS SINGLE
PI = 3.14159
PRINT "Main initial: PI = "; PI

SUB DoublePi()
    PI = PI * 2
    PRINT "In SUB: PI = "; PI
END SUB

DoublePi()
PRINT "Main after SUB: PI = "; PI
PRINT ""

REM Test 3: String global
DIM MSG AS STRING
MSG = "Hello"
PRINT "Main initial: MSG = "; MSG

FUNCTION AppendWorld() AS STRING
    MSG = MSG + " World"
    RETURN MSG
END FUNCTION

DIM result AS STRING
result = AppendWorld()
PRINT "FUNCTION returned: "; result
PRINT "Main after FUNCTION: MSG = "; MSG
PRINT ""

PRINT "All BUG-030 tests passed!"
END
