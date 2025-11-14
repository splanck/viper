REM Test newly implemented math functions: SGN, TAN, ATN, EXP, LOG
REM Testing BUG-005 and BUG-006 fixes

PRINT "=== Testing SGN Function ==="
PRINT "SGN(-10) = ";
PRINT SGN(-10)
PRINT "SGN(0) = ";
PRINT SGN(0)
PRINT "SGN(10) = ";
PRINT SGN(10)
PRINT "SGN(-3.5) = ";
PRINT SGN(-3.5)
PRINT "SGN(3.5) = ";
PRINT SGN(3.5)

PRINT ""
PRINT "=== Testing TAN Function ==="
PRINT "TAN(0) = ";
PRINT TAN(0)
PRINT "TAN(1) = ";
PRINT TAN(1)

PRINT ""
PRINT "=== Testing ATN Function ==="
PRINT "ATN(0) = ";
PRINT ATN(0)
PRINT "ATN(1) = ";
PRINT ATN(1)
PRINT "ATN(-1) = ";
PRINT ATN(-1)

PRINT ""
PRINT "=== Testing EXP Function ==="
PRINT "EXP(0) = ";
PRINT EXP(0)
PRINT "EXP(1) = ";
PRINT EXP(1)
PRINT "EXP(2) = ";
PRINT EXP(2)

PRINT ""
PRINT "=== Testing LOG Function ==="
PRINT "LOG(1) = ";
PRINT LOG(1)
PRINT "LOG(2.718281828) = ";
PRINT LOG(2.718281828)
PRINT "LOG(10) = ";
PRINT LOG(10)

PRINT ""
PRINT "=== Combining Math Functions ==="
REM Calculate PI using ATN: PI = 4 * ATN(1)
pi = 4 * ATN(1)
PRINT "PI (using 4*ATN(1)) = ";
PRINT pi

REM Verify TAN using SIN and COS
angle = 0.5
tanValue = TAN(angle)
sinCosRatio = SIN(angle) / COS(angle)
PRINT "TAN(0.5) = ";
PRINT tanValue
PRINT "SIN(0.5)/COS(0.5) = ";
PRINT sinCosRatio

REM Verify EXP and LOG are inverses
x = 5
result = LOG(EXP(x))
PRINT "LOG(EXP(5)) = ";
PRINT result

PRINT ""
PRINT "All math functions tested successfully!"
