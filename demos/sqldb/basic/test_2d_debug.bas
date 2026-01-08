' Minimal test case for Bug #020 with simpler values
PRINT "=== Bug #020 Debug Test ==="

SUB TestSimple()
    DIM arr(1, 2) AS INTEGER

    PRINT ""
    PRINT "--- DIM arr(1, 2) ---"
    PRINT "Expected array size: 2 x 3 = 6 elements"
    PRINT ""

    PRINT "Writing arr(0,0)=0"
    arr(0, 0) = 0
    PRINT "arr(0,0)="; arr(0, 0)

    PRINT ""
    PRINT "Writing arr(0,1)=1"
    arr(0, 1) = 1
    PRINT "arr(0,1)="; arr(0, 1)
    PRINT "arr(0,0)="; arr(0, 0)

    PRINT ""
    PRINT "Writing arr(0,2)=2"
    arr(0, 2) = 2
    PRINT "arr(0,2)="; arr(0, 2)
    PRINT "arr(0,1)="; arr(0, 1)
    PRINT "arr(0,0)="; arr(0, 0)

    PRINT ""
    PRINT "Writing arr(1,0)=10"
    arr(1, 0) = 10
    PRINT "arr(1,0)="; arr(1, 0)
    PRINT "arr(0,2)="; arr(0, 2)
    PRINT "arr(0,1)="; arr(0, 1)
    PRINT "arr(0,0)="; arr(0, 0)

    PRINT ""
    PRINT "=== Final read of all values ==="
    PRINT "arr(0,0)="; arr(0, 0); " expected 0"
    PRINT "arr(0,1)="; arr(0, 1); " expected 1"
    PRINT "arr(0,2)="; arr(0, 2); " expected 2"
    PRINT "arr(1,0)="; arr(1, 0); " expected 10"
    PRINT "arr(1,1)="; arr(1, 1); " expected 0 (not written)"
    PRINT "arr(1,2)="; arr(1, 2); " expected 0 (not written)"
END SUB

TestSimple()
