REM BUG-107: Test RETURN statement in FUNCTION
REM Should allow early return after setting return value

FUNCTION TestReturn(n AS INTEGER) AS BOOLEAN
    IF n > 10 THEN
        LET TestReturn = TRUE
        RETURN
    END IF
    LET TestReturn = FALSE
END FUNCTION

DIM result AS BOOLEAN
LET result = TestReturn(15)
PRINT "TestReturn(15) = "; result

LET result = TestReturn(5)
PRINT "TestReturn(5) = "; result
