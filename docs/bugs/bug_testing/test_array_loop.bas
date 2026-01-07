REM Test array with loops

PRINT "Test: Array with FOR loop"
DIM numbers(5) AS INTEGER
DIM i AS INTEGER

PRINT "Writing to array..."
FOR i = 0 TO 4
    numbers(i) = i * 10
NEXT i
PRINT "✓ Write complete"

PRINT "Reading from array..."
FOR i = 0 TO 4
    PRINT "numbers("; i; ") = "; numbers(i)
NEXT i

PRINT "Done!"
