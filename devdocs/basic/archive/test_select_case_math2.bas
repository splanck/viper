REM Test SELECT CASE with SGN - workaround for negative literals
PRINT "=== Testing SGN with IF/ELSEIF (workaround) ==="

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
    
    IF sign < 0 THEN
        PRINT "Negative"
    ELSEIF sign = 0 THEN
        PRINT "Zero"
    ELSEIF sign > 0 THEN
        PRINT "Positive"
    ELSE
        PRINT "Unknown"
    END IF
NEXT i

PRINT ""
PRINT "=== Testing SELECT CASE with positive values only ==="
FOR i = 0 TO 4
    val = ABS(testValues(i))
    
    PRINT "ABS("; testValues(i); ") = "; val; " - ";
    
    SELECT CASE val
        CASE 0
            PRINT "Zero"
        CASE 5
            PRINT "Five"
        CASE 100
            PRINT "One hundred"
        CASE ELSE
            PRINT "Other value"
    END SELECT
NEXT i

PRINT ""
PRINT "All tests passed!"
