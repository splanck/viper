' Test case for Bug #020: 2D Array Indexing Bug
' This test isolates the 2D array behavior to identify root cause

PRINT "=== Bug #020 Test: 2D Array Indexing ==="

' Test 1: Module-level 2D array
DIM arr2d(2, 3) AS INTEGER

PRINT ""
PRINT "--- Test 1: Module-level 2D array ---"
PRINT "Writing values..."
arr2d(0, 0) = 10
arr2d(0, 1) = 11
arr2d(0, 2) = 12
arr2d(1, 0) = 20
arr2d(1, 1) = 21
arr2d(1, 2) = 22

PRINT "Reading back..."
PRINT "arr2d(0,0)="; arr2d(0, 0); " expected 10"
PRINT "arr2d(0,1)="; arr2d(0, 1); " expected 11"
PRINT "arr2d(0,2)="; arr2d(0, 2); " expected 12"
PRINT "arr2d(1,0)="; arr2d(1, 0); " expected 20"
PRINT "arr2d(1,1)="; arr2d(1, 1); " expected 21"
PRINT "arr2d(1,2)="; arr2d(1, 2); " expected 22"

' Test 2: Local 2D array in SUB
SUB TestLocal2D()
    DIM local2d(2, 3) AS INTEGER

    PRINT ""
    PRINT "--- Test 2: Local 2D array in SUB ---"
    PRINT "Writing values..."
    local2d(0, 0) = 100
    local2d(0, 1) = 101
    local2d(0, 2) = 102
    local2d(1, 0) = 200
    local2d(1, 1) = 201
    local2d(1, 2) = 202

    PRINT "Reading back..."
    PRINT "local2d(0,0)="; local2d(0, 0); " expected 100"
    PRINT "local2d(0,1)="; local2d(0, 1); " expected 101"
    PRINT "local2d(0,2)="; local2d(0, 2); " expected 102"
    PRINT "local2d(1,0)="; local2d(1, 0); " expected 200"
    PRINT "local2d(1,1)="; local2d(1, 1); " expected 201"
    PRINT "local2d(1,2)="; local2d(1, 2); " expected 202"
END SUB

TestLocal2D()

' Test 3: Write and immediate read vs delayed read
SUB TestDelayedRead()
    DIM delayed2d(2, 3) AS INTEGER
    DIM i AS INTEGER
    DIM j AS INTEGER

    PRINT ""
    PRINT "--- Test 3: Write-then-immediate-read vs delayed read ---"

    ' Write all values
    FOR i = 0 TO 1
        FOR j = 0 TO 2
            delayed2d(i, j) = i * 10 + j
            PRINT "Wrote delayed2d("; i; ","; j; ")="; delayed2d(i, j)
        NEXT j
    NEXT i

    PRINT ""
    PRINT "Now reading all values after loop completed:"
    FOR i = 0 TO 1
        FOR j = 0 TO 2
            PRINT "delayed2d("; i; ","; j; ")="; delayed2d(i, j); " expected "; i * 10 + j
        NEXT j
    NEXT i
END SUB

TestDelayedRead()

' Test 4: Copy from 1D to 2D array (like groupRows in executor)
SUB TestCopyTo2D()
    DIM source(4) AS INTEGER
    DIM target(1, 4) AS INTEGER
    DIM i AS INTEGER

    PRINT ""
    PRINT "--- Test 4: Copy from 1D to 2D array ---"

    ' Initialize source
    FOR i = 0 TO 4
        source(i) = i * 100
    NEXT i

    ' Copy to target row 0
    PRINT "Copying source to target(0, *)..."
    FOR i = 0 TO 4
        target(0, i) = source(i)
        PRINT "Copied: source("; i; ")="; source(i); " -> target(0,"; i; ")="; target(0, i)
    NEXT i

    PRINT ""
    PRINT "Reading target(0, *) after all copies:"
    FOR i = 0 TO 4
        PRINT "target(0,"; i; ")="; target(0, i); " expected "; i * 100
    NEXT i
END SUB

TestCopyTo2D()

PRINT ""
PRINT "=== Test Complete ==="
