REM Test 1: Main -> SUB -> Main (INTEGER)
DIM COUNTER AS INTEGER
COUNTER = 100

PRINT "=== Test 1: Main -> SUB -> Main (INTEGER) ==="
PRINT "Main before SUB: COUNTER = " + STR$(COUNTER)

SUB IncrementCounter()
    PRINT "In SUB before increment: COUNTER = " + STR$(COUNTER)
    COUNTER = COUNTER + 50
    PRINT "In SUB after increment: COUNTER = " + STR$(COUNTER)
END SUB

IncrementCounter()
PRINT "Main after SUB: COUNTER = " + STR$(COUNTER)
PRINT ""

END
