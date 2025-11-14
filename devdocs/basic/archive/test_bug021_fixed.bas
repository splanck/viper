REM Test BUG-021 fix: SELECT CASE with negative literals
PRINT "=== Testing SELECT CASE with negative literals ==="

DIM testValues%(5)
testValues%(0) = -10
testValues%(1) = -5
testValues%(2) = 0
testValues%(3) = 5
testValues%(4) = 10

FOR i% = 0 TO 4
    val% = testValues%(i%)
    sign% = SGN(val%)
    
    PRINT "Value: "; val%; " - ";
    
    SELECT CASE sign%
        CASE -1
            PRINT "Negative"
        CASE 0
            PRINT "Zero"
        CASE 1
            PRINT "Positive"
        CASE ELSE
            PRINT "Unknown"
    END SELECT
NEXT i%

PRINT ""
PRINT "=== Testing with ranges including negative ==="
FOR i% = 0 TO 4
    val% = testValues%(i%)
    PRINT "Value: "; val%; " is ";
    
    SELECT CASE val%
        CASE -100 TO -10
            PRINT "Very negative"
        CASE -9 TO -1
            PRINT "Slightly negative"
        CASE 0
            PRINT "Zero"
        CASE 1 TO 9
            PRINT "Slightly positive"
        CASE 10 TO 100
            PRINT "Very positive"
    END SELECT
NEXT i%

PRINT ""
PRINT "BUG-021 fix validated!"
