' Test: Arrays
' Tests: fixed arrays, multi-dimensional arrays

' Fixed-size array
DIM arr(5) AS INTEGER
FOR i% = 0 TO 5
    arr(i%) = i% * 10
NEXT i%

PRINT "=== Arrays Test ==="
PRINT "Fixed array (0-5):"
FOR i% = 0 TO 5
    PRINT "  arr("; i%; ") = "; arr(i%)
NEXT i%

' Multi-dimensional array
DIM grid(2, 2) AS INTEGER
grid(0, 0) = 1 : grid(0, 1) = 2 : grid(0, 2) = 3
grid(1, 0) = 4 : grid(1, 1) = 5 : grid(1, 2) = 6
grid(2, 0) = 7 : grid(2, 1) = 8 : grid(2, 2) = 9

PRINT ""
PRINT "2D array (3x3):"
FOR r% = 0 TO 2
    PRINT "  ";
    FOR c% = 0 TO 2
        PRINT grid(r%, c%); " ";
    NEXT c%
    PRINT ""
NEXT r%

' Array bounds
PRINT ""
PRINT "Array bounds:"
PRINT "  LBOUND(arr) = "; LBOUND(arr)
PRINT "  UBOUND(arr) = "; UBOUND(arr)

' String array
DIM names$(3)
names$(0) = "Alice"
names$(1) = "Bob"
names$(2) = "Charlie"
names$(3) = "Diana"

PRINT ""
PRINT "String array:"
FOR i% = 0 TO 3
    PRINT "  names("; i%; ") = "; names$(i%)
NEXT i%

' Note about Collections:
' Viper.Collections.List/Map work with object types
' Requires boxing for primitive values
PRINT ""
PRINT "Note: Collections require object boxing"
PRINT "  (testing skipped for simplicity)"

PRINT "=== Arrays test complete ==="
