REM Test BUG-019 FIX: Float CONST preservation
REM Tests that CONST without suffix but with float initializer stays float

REM Test 1: Float constant without suffix
CONST PI = 3.14159
PRINT "PI = "; PI

REM Test 2: Integer constant without suffix
CONST A = 2
PRINT "A = "; A

REM Test 3: Float constant with # suffix
CONST Z# = 1.5
PRINT "Z# = "; Z#

REM Test 4: String constant
CONST MSG$ = "Hello"
PRINT "MSG$ = "; MSG$

REM Test 5: Multiple float constants
CONST E = 2.71828
CONST HALF = 0.5
PRINT "E = "; E
PRINT "HALF = "; HALF

REM Test 6: Using constants in expressions
PRINT "PI * 2 = "; PI * 2
PRINT "A + 3 = "; A + 3

PRINT "All CONST tests passed!"
END
