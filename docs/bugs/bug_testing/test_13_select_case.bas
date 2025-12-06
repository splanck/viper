' Test 13: SELECT CASE
DIM choice AS INTEGER

FOR choice = 1 TO 5
    SELECT CASE choice
        CASE 1
            PRINT "One"
        CASE 2
            PRINT "Two"
        CASE 3
            PRINT "Three"
        CASE ELSE
            PRINT "Other: "; choice
    END SELECT
NEXT choice
END
