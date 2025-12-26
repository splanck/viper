REM Test SELECT CASE with SGN and math functions
PRINT "=== Testing SELECT CASE with SGN ==="

DIM testValues(5)
testValues(0) = -100
testValues(1) = -5
testValues(2) = 0
testValues(3) = 5
testValues(4) = 100

FOR i = 0 TO 4
    val = testValues(i)
    sign = SGN(val)
    
    PRINT "Value: "; val; " - ";
    
    SELECT CASE sign
        CASE -1
            PRINT "Negative"
        CASE 0
            PRINT "Zero"
        CASE 1
            PRINT "Positive"
        CASE ELSE
            PRINT "Unknown"
    END SELECT
NEXT i

PRINT ""
PRINT "=== Testing nested IF with math ==="
angle = 1

sinVal = SIN(angle)
IF sinVal > 0 THEN
    PRINT "SIN(1) is positive"
    
    cosVal = COS(angle)
    IF cosVal > 0 THEN
        PRINT "COS(1) is positive - First quadrant"
    ELSE
        PRINT "COS(1) is negative - Second quadrant"
    END IF
ELSE
    PRINT "SIN(1) is not positive"
END IF

PRINT ""
PRINT "All SELECT CASE tests passed!"
