REM Test 6: FUNCTION -> FUNCTION communication
DIM DATA AS INTEGER
DATA = 15

PRINT "=== Test 6: FUNCTION -> FUNCTION communication ==="
PRINT "Main initial: DATA = " + STR$(DATA)

FUNCTION FirstFunc() AS INTEGER
    PRINT "FirstFunc before: DATA = " + STR$(DATA)
    DATA = 30
    PRINT "FirstFunc after: DATA = " + STR$(DATA)
    RETURN DATA
END FUNCTION

FUNCTION SecondFunc() AS INTEGER
    PRINT "SecondFunc reads: DATA = " + STR$(DATA)
    RETURN DATA
END FUNCTION

DIM r1 AS INTEGER
DIM r2 AS INTEGER

r1 = FirstFunc()
PRINT "FirstFunc returned: " + STR$(r1)
PRINT "Main after FirstFunc: DATA = " + STR$(DATA)

r2 = SecondFunc()
PRINT "SecondFunc returned: " + STR$(r2)
PRINT "Main final: DATA = " + STR$(DATA)
PRINT ""

END
