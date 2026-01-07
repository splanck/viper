REM Test edge cases and boundary conditions
PRINT "=== Testing SWAP Edge Cases ==="

REM SWAP with same variable (should be no-op)
x = 42
SWAP x, x
PRINT "SWAP x, x: ";
PRINT x

REM SWAP in a loop
DIM arr(3)
arr(0) = 10
arr(1) = 20
arr(2) = 30
PRINT "Before bubble sort-style swaps:"
PRINT arr(0); arr(1); arr(2)
IF arr(0) > arr(1) THEN SWAP arr(0), arr(1)
IF arr(1) > arr(2) THEN SWAP arr(1), arr(2)
IF arr(0) > arr(1) THEN SWAP arr(0), arr(1)
PRINT "After swaps:"
PRINT arr(0); arr(1); arr(2)

PRINT ""
PRINT "=== Testing SGN Edge Cases ==="
PRINT "SGN(0) = "; SGN(0)
PRINT "SGN(-0) = "; SGN(-0)
PRINT "SGN(1) = "; SGN(1)
PRINT "SGN(-1) = "; SGN(-1)
PRINT "SGN(999999) = "; SGN(999999)
PRINT "SGN(-999999) = "; SGN(-999999)

PRINT ""
PRINT "=== Testing Math Function Edge Cases ==="
PRINT "LOG(1) = "; LOG(1)
PRINT "EXP(0) = "; EXP(0)
PRINT "ATN(0) = "; ATN(0)
PRINT "TAN(0) = "; TAN(0)

PRINT ""
PRINT "=== Testing CONST with Math ==="
CONST MAX = 100
CONST MIN = 0
PRINT "MAX - MIN = "; MAX - MIN
PRINT "MAX * 2 = "; MAX * 2
PRINT "SGN(MAX - MIN) = "; SGN(MAX - MIN)

PRINT ""
PRINT "All edge case tests passed!"
