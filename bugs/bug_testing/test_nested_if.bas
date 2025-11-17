REM Test nested IF/ELSE IF
FUNCTION TestFunc() AS INTEGER
    DIM roll AS INTEGER
    roll = 50
    IF roll < 65 THEN
        TestFunc = 0
    ELSE IF roll < 85 THEN
        TestFunc = 1
    ELSE IF roll < 93 THEN
        TestFunc = 2
    ELSE IF roll < 98 THEN
        TestFunc = 3
    ELSE
        TestFunc = 4
    END IF
END FUNCTION

PRINT "Result: "; TestFunc()
