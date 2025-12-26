REM Test complex boolean logic
DIM a AS INTEGER
DIM b AS INTEGER
DIM c AS INTEGER
DIM result AS INTEGER

PRINT "=== BOOLEAN LOGIC TEST ==="
PRINT ""

a = 5
b = 10
c = 3

PRINT "a = "; a; ", b = "; b; ", c = "; c
PRINT ""

REM Test AND
PRINT "Testing AND:"
IF a > 0 AND b > 0 THEN
    PRINT "  a > 0 AND b > 0: TRUE ✓"
ELSE
    PRINT "  a > 0 AND b > 0: FALSE ✗"
END IF

IF a > 10 AND b > 0 THEN
    PRINT "  a > 10 AND b > 0: TRUE ✗"
ELSE
    PRINT "  a > 10 AND b > 0: FALSE ✓"
END IF
PRINT ""

REM Test OR
PRINT "Testing OR:"
IF a > 0 OR b < 0 THEN
    PRINT "  a > 0 OR b < 0: TRUE ✓"
ELSE
    PRINT "  a > 0 OR b < 0: FALSE ✗"
END IF

IF a < 0 OR b < 0 THEN
    PRINT "  a < 0 OR b < 0: TRUE ✗"
ELSE
    PRINT "  a < 0 OR b < 0: FALSE ✓"
END IF
PRINT ""

REM Test NOT
PRINT "Testing NOT:"
IF NOT (a < 0) THEN
    PRINT "  NOT (a < 0): TRUE ✓"
ELSE
    PRINT "  NOT (a < 0): FALSE ✗"
END IF

IF NOT (a > 0) THEN
    PRINT "  NOT (a > 0): TRUE ✗"
ELSE
    PRINT "  NOT (a > 0): FALSE ✓"
END IF
PRINT ""

REM Test complex combinations
PRINT "Testing combinations:"
IF (a > 0 AND b > 5) OR c < 5 THEN
    PRINT "  (a > 0 AND b > 5) OR c < 5: TRUE ✓"
ELSE
    PRINT "  (a > 0 AND b > 5) OR c < 5: FALSE ✗"
END IF

IF a > 0 AND (b > 5 OR c > 10) THEN
    PRINT "  a > 0 AND (b > 5 OR c > 10): TRUE ✓"
ELSE
    PRINT "  a > 0 AND (b > 5 OR c > 10): FALSE ✗"
END IF
PRINT ""

REM Test short-circuit (if supported)
PRINT "Testing precedence:"
IF a < 10 OR b < 5 AND c > 0 THEN
    PRINT "  a < 10 OR b < 5 AND c > 0: (depends on precedence)"
END IF

IF (a < 10 OR b < 5) AND c > 0 THEN
    PRINT "  (a < 10 OR b < 5) AND c > 0: TRUE ✓"
END IF
PRINT ""

PRINT "Boolean logic test complete!"
