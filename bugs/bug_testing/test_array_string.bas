REM Test string arrays

PRINT "Test: String array"
DIM names(3) AS STRING
DIM i AS INTEGER

names(0) = "Alice"
names(1) = "Bob"
names(2) = "Charlie"

PRINT "Reading strings..."
FOR i = 0 TO 2
    PRINT "names("; i; ") = "; names(i)
NEXT i

PRINT "Done!"
