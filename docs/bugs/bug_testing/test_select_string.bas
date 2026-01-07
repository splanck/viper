REM SELECT CASE with STRING test

DIM cmd AS STRING
cmd = "north"

SELECT CASE cmd
    CASE "north"
        PRINT "Going north"
    CASE "south"
        PRINT "Going south"
    CASE ELSE
        PRINT "Unknown"
END SELECT

PRINT "Done"
