' Test case for Bug #010: 2D array with CONST dimensions
PRINT "=== Bug #010 Test: CONST Array Dimensions ==="

CONST MAX_ROWS = 5
CONST MAX_COLS = 3

SUB TestConstDimensions()
    DIM arr(MAX_ROWS, MAX_COLS) AS INTEGER

    PRINT ""
    PRINT "--- DIM arr(MAX_ROWS, MAX_COLS) where MAX_ROWS=5, MAX_COLS=3 ---"
    PRINT "Expected array size: 6 x 4 = 24 elements (0-based indexing)"
    PRINT ""

    ' Write to several positions
    arr(0, 0) = 100
    arr(0, 1) = 101
    arr(0, 2) = 102
    arr(1, 0) = 110
    arr(2, 1) = 121
    arr(3, 2) = 132
    arr(4, 0) = 140
    arr(5, 3) = 153

    PRINT "After writes:"
    PRINT "arr(0, 0) = "; arr(0, 0); " expected 100"
    PRINT "arr(0, 1) = "; arr(0, 1); " expected 101"
    PRINT "arr(0, 2) = "; arr(0, 2); " expected 102"
    PRINT "arr(1, 0) = "; arr(1, 0); " expected 110"
    PRINT "arr(2, 1) = "; arr(2, 1); " expected 121"
    PRINT "arr(3, 2) = "; arr(3, 2); " expected 132"
    PRINT "arr(4, 0) = "; arr(4, 0); " expected 140"
    PRINT "arr(5, 3) = "; arr(5, 3); " expected 153"

    PRINT ""
    PRINT "Test result:"
    IF arr(0, 0) = 100 AND arr(0, 1) = 101 AND arr(0, 2) = 102 AND arr(1, 0) = 110 AND arr(2, 1) = 121 AND arr(3, 2) = 132 AND arr(4, 0) = 140 AND arr(5, 3) = 153 THEN
        PRINT "PASS: All values read correctly!"
    ELSE
        PRINT "FAIL: Values were corrupted"
    END IF
END SUB

TestConstDimensions()
