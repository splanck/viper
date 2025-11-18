REM Test SELECT CASE with string assignment

FUNCTION GetName(n AS INTEGER) AS STRING
    DIM s AS STRING
    SELECT CASE n
        CASE 1
            s = "Alice"
        CASE 2
            s = "Bob"
        CASE ELSE
            s = "Unknown"
    END SELECT
    GetName = s
END FUNCTION

PRINT "Test 1: "; GetName(1)
PRINT "Test 2: "; GetName(2)
PRINT "Test 3: "; GetName(99)
