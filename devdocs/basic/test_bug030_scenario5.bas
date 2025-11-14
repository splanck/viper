REM Test 5: SUB -> SUB communication
DIM STATE AS INTEGER
STATE = 10

PRINT "=== Test 5: SUB -> SUB communication ==="
PRINT "Main initial: STATE = " + STR$(STATE)

SUB FirstSub()
    PRINT "FirstSub before: STATE = " + STR$(STATE)
    STATE = 20
    PRINT "FirstSub after: STATE = " + STR$(STATE)
END SUB

SUB SecondSub()
    PRINT "SecondSub reads: STATE = " + STR$(STATE)
END SUB

FirstSub()
PRINT "Main after FirstSub: STATE = " + STR$(STATE)

SecondSub()
PRINT "Main after SecondSub: STATE = " + STR$(STATE)
PRINT ""

END
