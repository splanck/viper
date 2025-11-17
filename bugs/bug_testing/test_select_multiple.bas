REM Multiple SELECT CASE statements

DIM cmd AS STRING

cmd = "north"
SELECT CASE cmd
    CASE "north"
        PRINT "Test 1: Going north"
    CASE "south"
        PRINT "Test 1: Going south"
END SELECT

cmd = "south"
SELECT CASE cmd
    CASE "north"
        PRINT "Test 2: Going north"
    CASE "south"
        PRINT "Test 2: Going south"
END SELECT

PRINT "Done"
