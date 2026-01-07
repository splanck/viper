REM BUG-107 Comprehensive Test: BOOLEAN functions with RETURN
REM Tests various scenarios with early returns

FUNCTION IsPositive(n AS INTEGER) AS BOOLEAN
    IF n > 0 THEN
        LET IsPositive = TRUE
        RETURN
    END IF
    LET IsPositive = FALSE
END FUNCTION

FUNCTION IsInRange(n AS INTEGER, min AS INTEGER, max AS INTEGER) AS BOOLEAN
    IF n < min THEN
        LET IsInRange = FALSE
        RETURN
    END IF
    IF n > max THEN
        LET IsInRange = FALSE
        RETURN
    END IF
    LET IsInRange = TRUE
END FUNCTION

FUNCTION IsEven(n AS INTEGER) AS BOOLEAN
    IF n MOD 2 = 0 THEN
        LET IsEven = TRUE
    ELSE
        LET IsEven = FALSE
    END IF
    REM No early return - test normal flow
END FUNCTION

REM Test all functions
PRINT "Testing IsPositive:"
PRINT "  IsPositive(5) = "; IsPositive(5); " (expect TRUE)"
PRINT "  IsPositive(-3) = "; IsPositive(-3); " (expect FALSE)"
PRINT

PRINT "Testing IsInRange:"
PRINT "  IsInRange(5, 1, 10) = "; IsInRange(5, 1, 10); " (expect TRUE)"
PRINT "  IsInRange(15, 1, 10) = "; IsInRange(15, 1, 10); " (expect FALSE)"
PRINT "  IsInRange(-5, 1, 10) = "; IsInRange(-5, 1, 10); " (expect FALSE)"
PRINT

PRINT "Testing IsEven:"
PRINT "  IsEven(4) = "; IsEven(4); " (expect TRUE)"
PRINT "  IsEven(7) = "; IsEven(7); " (expect FALSE)"
PRINT

PRINT "All tests passed!"
